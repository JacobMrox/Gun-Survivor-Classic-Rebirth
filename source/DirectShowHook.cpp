#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <uuids.h>
#include <string>
#include <cwctype>
#include <cstring>
#include <cwchar>
#include <vector>
#include "MinHook.h"
#include "DirectShowHook.h"
#include "AviFfmpegHook.h"
#include "debug.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "strmiids.lib")

namespace
{
    static bool gSkipMovies = false;
    // CLSID_NullRenderer is omitted by some current Windows SDK header sets.
    // This is Microsoft's registered DirectShow Null Renderer class ID.
    static const CLSID CLSID_SurvivorNullRenderer =
        { 0xC1F400A4, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };

    static bool IsMovieFile(const wchar_t* path) {
        if (!path) return false;
        const wchar_t* dot = wcsrchr(path, L'.');
        return dot && (_wcsicmp(dot, L".avi") == 0 || _wcsicmp(dot, L".mpg") == 0 || _wcsicmp(dot, L".mpeg") == 0);
    }

    static void HideDirectShowVideo(IGraphBuilder* graph) {
        IVideoWindow* video = nullptr;
        if (graph && SUCCEEDED(graph->QueryInterface(IID_IVideoWindow, reinterpret_cast<void**>(&video))) && video) {
            video->put_AutoShow(OAFALSE);
            video->put_Visible(OAFALSE);
            video->put_WindowState(SW_HIDE);
            video->SetWindowPosition(-32000, -32000, 1, 1);
            video->Release();
        }
        IBasicVideo* basic = nullptr;
        if (graph && SUCCEEDED(graph->QueryInterface(IID_IBasicVideo, reinterpret_cast<void**>(&basic))) && basic) {
            basic->SetDestinationPosition(-32000, -32000, 1, 1);
            basic->Release();
        }
    }

    static void FreeMediaType(AM_MEDIA_TYPE& media) {
        if (media.cbFormat) CoTaskMemFree(media.pbFormat);
        if (media.pUnk) media.pUnk->Release();
        ZeroMemory(&media, sizeof(media));
    }

    // Survivor's legacy renderer can present directly into the game surface,
    // bypassing IVideoWindow visibility and producing alternating old/new
    // frames. Replace only terminal video renderers with DirectShow's null
    // renderer. Unlike removing the renderer, this keeps the video branch
    // connected so audio, timing and EC_COMPLETE continue to work.
    static bool ReplaceDirectShowVideoRenderersWithNull(IGraphBuilder* graph) {
        if (!graph) return false;
        IEnumFilters* filters = nullptr;
        if (FAILED(graph->EnumFilters(&filters)) || !filters) return false;
        std::vector<IBaseFilter*> renderers;
        IBaseFilter* filter = nullptr;
        while (filters->Next(1, &filter, nullptr) == S_OK) {
            IEnumPins* pins = nullptr;
            bool connectedVideoInput = false;
            bool hasOutput = false;
            if (SUCCEEDED(filter->EnumPins(&pins)) && pins) {
                IPin* pin = nullptr;
                while (pins->Next(1, &pin, nullptr) == S_OK) {
                    PIN_DIRECTION direction = PINDIR_INPUT;
                    pin->QueryDirection(&direction);
                    if (direction == PINDIR_OUTPUT) {
                        hasOutput = true;
                    } else {
                        IPin* peer = nullptr;
                        AM_MEDIA_TYPE media = {};
                        if (SUCCEEDED(pin->ConnectedTo(&peer)) && peer) {
                            if (SUCCEEDED(pin->ConnectionMediaType(&media))) {
                                connectedVideoInput = media.majortype == MEDIATYPE_Video;
                                FreeMediaType(media);
                            }
                            peer->Release();
                        }
                    }
                    pin->Release();
                }
                pins->Release();
            }
            if (connectedVideoInput && !hasOutput) {
                renderers.push_back(filter);  // keep enumeration reference
            } else {
                filter->Release();
            }
            filter = nullptr;
        }
        filters->Release();
        bool replaced = false;
        for (IBaseFilter* renderer : renderers) {
            IPin* oldInput = nullptr;
            IPin* upstream = nullptr;
            AM_MEDIA_TYPE media = {};
            IEnumPins* pins = nullptr;
            if (SUCCEEDED(renderer->EnumPins(&pins)) && pins) {
                IPin* pin = nullptr;
                while (pins->Next(1, &pin, nullptr) == S_OK) {
                    PIN_DIRECTION direction = PINDIR_OUTPUT;
                    pin->QueryDirection(&direction);
                    IPin* peer = nullptr;
                    AM_MEDIA_TYPE candidate = {};
                    if (direction == PINDIR_INPUT && SUCCEEDED(pin->ConnectedTo(&peer)) && peer &&
                        SUCCEEDED(pin->ConnectionMediaType(&candidate)) && candidate.majortype == MEDIATYPE_Video) {
                        oldInput = pin;
                        upstream = peer;
                        media = candidate;
                        break;
                    }
                    if (peer) peer->Release();
                    FreeMediaType(candidate);
                    pin->Release();
                }
                pins->Release();
            }

            IBaseFilter* nullRenderer = nullptr;
            IPin* nullInput = nullptr;
            HRESULT hr = oldInput && upstream
                ? CoCreateInstance(CLSID_SurvivorNullRenderer, nullptr, CLSCTX_INPROC_SERVER,
                    IID_IBaseFilter, reinterpret_cast<void**>(&nullRenderer))
                : E_FAIL;
            if (SUCCEEDED(hr)) hr = graph->AddFilter(nullRenderer, L"Survivor Null Video Renderer");
            if (SUCCEEDED(hr)) {
                IEnumPins* nullPins = nullptr;
                if (SUCCEEDED(nullRenderer->EnumPins(&nullPins)) && nullPins) {
                    IPin* pin = nullptr;
                    while (nullPins->Next(1, &pin, nullptr) == S_OK) {
                        PIN_DIRECTION direction = PINDIR_OUTPUT;
                        pin->QueryDirection(&direction);
                        if (direction == PINDIR_INPUT) { nullInput = pin; break; }
                        pin->Release();
                    }
                    nullPins->Release();
                }
                if (!nullInput) hr = E_NOINTERFACE;
            }
            if (SUCCEEDED(hr)) {
                graph->Disconnect(upstream);
                graph->Disconnect(oldInput);
                hr = graph->ConnectDirect(upstream, nullInput, &media);
                if (FAILED(hr)) hr = graph->Connect(upstream, nullInput);
                if (SUCCEEDED(hr)) {
                    const HRESULT removeHr = graph->RemoveFilter(renderer);
                    replaced = true;
                    LogLine("[MOVIE] replaced DirectShow video renderer with null renderer: connect=%08lX remove=%08lX",
                        static_cast<unsigned long>(hr), static_cast<unsigned long>(removeHr));
                } else {
                    graph->Disconnect(upstream);
                    graph->Disconnect(nullInput);
                    HRESULT restoreHr = graph->ConnectDirect(upstream, oldInput, &media);
                    if (FAILED(restoreHr)) restoreHr = graph->Connect(upstream, oldInput);
                    graph->RemoveFilter(nullRenderer);
                    LogLine("[MOVIE] null renderer connection failed: %08lX; restored legacy renderer: %08lX",
                        static_cast<unsigned long>(hr), static_cast<unsigned long>(restoreHr));
                }
            } else if (nullRenderer) {
                graph->RemoveFilter(nullRenderer);
                LogLine("[MOVIE] null renderer creation failed: %08lX", static_cast<unsigned long>(hr));
            }
            if (nullInput) nullInput->Release();
            if (nullRenderer) nullRenderer->Release();
            if (upstream) upstream->Release();
            if (oldInput) oldInput->Release();
            FreeMediaType(media);
            renderer->Release();
        }
        return replaced;
    }

    struct HideVideoRequest { IGraphBuilder* graph = nullptr; };
    static DWORD WINAPI HideDirectShowVideoLater(LPVOID parameter) {
        auto* request = static_cast<HideVideoRequest*>(parameter);
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // Survivor connects the renderer after AddSourceFilter.  Suppress every
        // stage of that short construction window, not just the first attempt.
        Sleep(250);
        HideDirectShowVideo(request->graph);
        Sleep(750);
        HideDirectShowVideo(request->graph);
        Sleep(1000);
        HideDirectShowVideo(request->graph);
        if (SUCCEEDED(apartment)) CoUninitialize();
        request->graph->Release();
        delete request;
        return 0;
    }

    static void QueueDirectShowVideoHide(IGraphBuilder* graph) {
        if (!graph) return;
        auto* request = new HideVideoRequest{ graph };
        graph->AddRef();
        if (!CreateThread(nullptr, 0, HideDirectShowVideoLater, request, 0, nullptr)) { graph->Release(); delete request; }
    }

    struct GraphState
    {
        bool skipTriggered = false;
        bool ffmpegVideo = false;
        unsigned long ffmpegGeneration = 0;
        unsigned long overlayGeneration = 0;
        HWND gameHWnd = NULL;
        IGraphBuilder* pGraph = nullptr;
        IMediaControl* pControl = nullptr;
        IMediaEvent* pEvent = nullptr;
    };

    struct MediaControlProxy : IMediaControl
    {
        IMediaControl* real = nullptr;
        GraphState* st = nullptr;
        LONG ref = 1;

        MediaControlProxy(IMediaControl* r, GraphState* s) : real(r), st(s) { if (real) real->AddRef(); }
        ~MediaControlProxy() { if (real) real->Release(); }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
            if (riid == IID_IUnknown || riid == IID_IMediaControl || riid == IID_IDispatch) {
                *ppv = static_cast<IMediaControl*>(this); AddRef(); return S_OK;
            }
            return real->QueryInterface(riid, ppv);
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref); }
        ULONG STDMETHODCALLTYPE Release() override {
            ULONG r = (ULONG)InterlockedDecrement(&ref);
            if (r == 0) delete this;
            return r;
        }

        // IDispatch boilerplates...
        HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* p) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT i, LCID l, ITypeInfo** pp) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID r, LPOLESTR* n, UINT c, LCID l, DISPID* d) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE Invoke(DISPID d, REFIID r, LCID l, WORD w, DISPPARAMS* p, VARIANT* v, EXCEPINFO* e, UINT* u) override { return E_NOTIMPL; }

        static DWORD WINAPI KillMovieThread(LPVOID lpParam) {
            GraphState* st = (GraphState*)lpParam;
            // The graph proxy can be released as soon as the movie completes,
            // while this polling thread is still scheduled.  Never retain its
            // embedded GraphState: take independent COM references and use
            // only the copied window/flags below.
            IGraphBuilder* graph = st ? st->pGraph : nullptr;
            IMediaControl* control = st ? st->pControl : nullptr;
            IMediaEvent* event = st ? st->pEvent : nullptr;
            const bool ffmpegVideo = st && st->ffmpegVideo;
            const unsigned long ffmpegGeneration = st ? st->ffmpegGeneration : 0;
            const unsigned long overlayGeneration = st ? st->overlayGeneration : 0;
            const bool gameOverlay = AviFfmpegHook::IsGameOverlayActive(overlayGeneration);
            const HWND gameHWnd = st ? st->gameHWnd : NULL;
            if (graph) graph->AddRef();
            if (control) control->AddRef();
            if (event) event->AddRef();

            auto finish = [&]() {
                if (event) event->Release();
                if (control) control->Release();
                if (graph) graph->Release();
            };
            if (!graph || !control) {
                finish();
                return 0;
            }

            HANDLE hEv = NULL;
            if (event) event->GetEventHandle((OAEVENT*)&hEv);

            // Input and configured skipping are handled exclusively by the
            // native MovieDone hook on the game's thread. This worker only
            // keeps the legacy renderer hidden and observes graph completion;
            // posting another EC_COMPLETE can be consumed by the next movie.
            while (!gSkipMovies) {
                if (ffmpegVideo && !AviFfmpegHook::IsDirectShowActive(ffmpegGeneration)) break;
                if (gameOverlay) {
                    if (!AviFfmpegHook::IsGameOverlayActive(overlayGeneration)) break;
                    HideDirectShowVideo(graph);
                }
                long code = 0;
                LONG_PTR p1 = 0, p2 = 0;
                if (event && event->GetEvent(&code, &p1, &p2, 0) == S_OK) {
                    event->FreeEventParams(code, p1, p2);
                    if (code == EC_COMPLETE || code == EC_USERABORT || code == EC_ERRORABORT) break;
                }
                Sleep(15);
            }
            finish();
            return 0;
        }

        HRESULT STDMETHODCALLTYPE Run() override {
            if (!st->skipTriggered) {
                if (!st->gameHWnd) st->gameHWnd = GetActiveWindow();
                st->pControl = real;
                st->pGraph->QueryInterface(IID_IMediaEvent, (void**)&st->pEvent);
                CreateThread(NULL, 0, KillMovieThread, st, 0, NULL);
            }
            if (st->ffmpegVideo || AviFfmpegHook::IsGameOverlayActive(st->overlayGeneration))
                ReplaceDirectShowVideoRenderersWithNull(st->pGraph);
            // Retain the window suppression as a fallback for unusual graphs
            // whose renderer could not be detached.
            const HRESULT hr = real->Run();
            if (st->ffmpegVideo || AviFfmpegHook::IsGameOverlayActive(st->overlayGeneration)) HideDirectShowVideo(st->pGraph);
            return hr;
        }

        // Other methods...
        HRESULT STDMETHODCALLTYPE Pause() override { return real->Pause(); }
        HRESULT STDMETHODCALLTYPE Stop() override {
            st->skipTriggered = true;
            if (st->ffmpegVideo) AviFfmpegHook::StopForDirectShow(st->ffmpegGeneration);
            if (AviFfmpegHook::IsGameOverlayActive(st->overlayGeneration))
                AviFfmpegHook::StopForGameOverlay(st->overlayGeneration);
            return real->Stop();
        }
        HRESULT STDMETHODCALLTYPE GetState(LONG ms, OAFilterState* pfs) override { return real->GetState(ms, pfs); }
        HRESULT STDMETHODCALLTYPE RenderFile(BSTR str) override { return real->RenderFile(str); }
        HRESULT STDMETHODCALLTYPE AddSourceFilter(BSTR str, IDispatch** pp) override { return real->AddSourceFilter(str, pp); }
        HRESULT STDMETHODCALLTYPE get_FilterCollection(IDispatch** pp) override { return real->get_FilterCollection(pp); }
        HRESULT STDMETHODCALLTYPE get_RegFilterCollection(IDispatch** pp) override { return real->get_RegFilterCollection(pp); }
        HRESULT STDMETHODCALLTYPE StopWhenReady() override { return real->StopWhenReady(); }
    };

    struct GraphBuilderProxy : IGraphBuilder
    {
        IGraphBuilder* real = nullptr;
        LONG ref = 1;
        GraphState st{};

        GraphBuilderProxy(IGraphBuilder* r) : real(r) { if (real) real->AddRef(); st.pGraph = r; }
        ~GraphBuilderProxy() {
            if (st.ffmpegVideo) AviFfmpegHook::StopForDirectShow(st.ffmpegGeneration);
            if (st.overlayGeneration) AviFfmpegHook::StopForGameOverlay(st.overlayGeneration);
            if (real) real->Release();
            if (st.pEvent) st.pEvent->Release();
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (riid == IID_IUnknown || riid == IID_IGraphBuilder) {
                *ppv = static_cast<IGraphBuilder*>(this);
                AddRef();
                return S_OK;
            }
            HRESULT hr = real->QueryInterface(riid, ppv);
            if (SUCCEEDED(hr) && riid == IID_IMediaControl) {
                IMediaControl* mc = (IMediaControl*)*ppv;
                *ppv = new MediaControlProxy(mc, &st);
                mc->Release(); return S_OK;
            }
            return hr;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref); }
        ULONG STDMETHODCALLTYPE Release() override {
            ULONG r = (ULONG)InterlockedDecrement(&ref);
            if (r == 0) delete this; return r;
        }
        // ... Forward all IGraphBuilder methods to real ...
        HRESULT STDMETHODCALLTYPE AddFilter(IBaseFilter* p, LPCWSTR n) override { return real->AddFilter(p, n); }
        HRESULT STDMETHODCALLTYPE RemoveFilter(IBaseFilter* p) override { return real->RemoveFilter(p); }
        HRESULT STDMETHODCALLTYPE EnumFilters(IEnumFilters** p) override { return real->EnumFilters(p); }
        HRESULT STDMETHODCALLTYPE FindFilterByName(LPCWSTR n, IBaseFilter** p) override { return real->FindFilterByName(n, p); }
        HRESULT STDMETHODCALLTYPE ConnectDirect(IPin* p1, IPin* p2, const AM_MEDIA_TYPE* m) override { return real->ConnectDirect(p1, p2, m); }
        HRESULT STDMETHODCALLTYPE Reconnect(IPin* p) override { return real->Reconnect(p); }
        HRESULT STDMETHODCALLTYPE Disconnect(IPin* p) override { return real->Disconnect(p); }
        HRESULT STDMETHODCALLTYPE SetDefaultSyncSource() override { return real->SetDefaultSyncSource(); }
        HRESULT STDMETHODCALLTYPE Connect(IPin* p1, IPin* p2) override { return real->Connect(p1, p2); }
        HRESULT STDMETHODCALLTYPE Render(IPin* p) override { return real->Render(p); }
        HRESULT STDMETHODCALLTYPE RenderFile(LPCWSTR f, LPCWSTR p) override {
            const HRESULT hr = real->RenderFile(f, p);
            if (SUCCEEDED(hr) && IsMovieFile(f)) {
                st.ffmpegGeneration = gSkipMovies ? 0 : AviFfmpegHook::StartForDirectShow(f);
                st.ffmpegVideo = st.ffmpegGeneration != 0;
                if (st.ffmpegVideo) {
                    HideDirectShowVideo(real);
                    LogLine("[MOVIE] FFmpeg display replacing DirectShow video: %S", f);
                } else {
                    LogLine("[MOVIE] FFmpeg could not open DirectShow video, keeping native renderer: %S", f);
                }
            }
            return hr;
        }
        HRESULT STDMETHODCALLTYPE AddSourceFilter(LPCWSTR f, LPCWSTR n, IBaseFilter** p) override {
            const HRESULT hr = real->AddSourceFilter(f, n, p);
            if (SUCCEEDED(hr) && IsMovieFile(f) && !gSkipMovies) {
                st.overlayGeneration = AviFfmpegHook::StartForGameOverlay(f);
                if (st.overlayGeneration) QueueDirectShowVideoHide(real);
            }
            return hr;
        }
        HRESULT STDMETHODCALLTYPE SetLogFile(DWORD_PTR h) override { return real->SetLogFile(h); }
        HRESULT STDMETHODCALLTYPE Abort() override { return real->Abort(); }
        HRESULT STDMETHODCALLTYPE ShouldOperationContinue() override { return real->ShouldOperationContinue(); }
    };

    // --- Hooks ---
    using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static CreateFileA_t Real_CreateFileA = nullptr;
    using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static CreateFileW_t Real_CreateFileW = nullptr;
    using GetFileAttributesA_t = DWORD(WINAPI*)(LPCSTR);
    static GetFileAttributesA_t Real_GetFileAttributesA = nullptr;
    using GetFileAttributesW_t = DWORD(WINAPI*)(LPCWSTR);
    static GetFileAttributesW_t Real_GetFileAttributesW = nullptr;

    static std::string DebugLogPathA() {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(GetModuleHandleA("d3d8.dll"), path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "debug.log");
        return path;
    }

    static std::wstring DebugLogPathW() {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(GetModuleHandleW(L"d3d8.dll"), path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"debug.log");
        return path;
    }

    static HANDLE WINAPI Hook_CreateFileA(LPCSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE h) {
        if (f) {
            std::string n = f;
            if (n.find("debug.txt") != std::string::npos) return Real_CreateFileA(DebugLogPathA().c_str(), a, s, sa, c, fl, h);
        }
        return Real_CreateFileA(f, a, s, sa, c, fl, h);
    }

    static HANDLE WINAPI Hook_CreateFileW(LPCWSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE h) {
        if (f) {
            std::wstring n = f;
            if (n.find(L"debug.txt") != std::wstring::npos) return Real_CreateFileW(DebugLogPathW().c_str(), a, s, sa, c, fl, h);
        }
        return Real_CreateFileW(f, a, s, sa, c, fl, h);
    }

    static DWORD WINAPI Hook_GetFileAttributesA(LPCSTR f) {
        return Real_GetFileAttributesA(f);
    }

    static DWORD WINAPI Hook_GetFileAttributesW(LPCWSTR f) {
        return Real_GetFileAttributesW(f);
    }

    // Typedefs for the "old school" IO
    using lopen_t = HFILE(WINAPI*)(LPCSTR, int);
    static lopen_t Real_lopen = nullptr;

    using lcreat_t = HFILE(WINAPI*)(LPCSTR, int);
    static lcreat_t Real_lcreat = nullptr;

    // This catches fopen and older CRT file calls
    HFILE WINAPI Hook_lopen(LPCSTR lpPathName, int iReadWrite) {
        if (lpPathName) {
            if (strstr(lpPathName, "debug.txt")) return Real_lopen(DebugLogPathA().c_str(), iReadWrite);
        }
        return Real_lopen(lpPathName, iReadWrite);
    }

    // This catches the creation of files via older APIs
    HFILE WINAPI Hook_lcreat(LPCSTR lpPathName, int iAttribute) {
        if (lpPathName && strstr(lpPathName, "debug.txt")) return Real_lcreat(DebugLogPathA().c_str(), iAttribute);
        return Real_lcreat(lpPathName, iAttribute);
    }

    using CoCreateInstance_t = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
    static CoCreateInstance_t Real_CoCreateInstance = nullptr;
    static HRESULT WINAPI Hook_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnk, DWORD dwCtx, REFIID riid, LPVOID* ppv) {
        HRESULT hr = Real_CoCreateInstance(rclsid, pUnk, dwCtx, riid, ppv);
        if (SUCCEEDED(hr) && ppv && *ppv && IsEqualCLSID(rclsid, CLSID_FilterGraph)) {
            // D3D_Movie.cpp creates the graph as IUnknown, then queries IGraphBuilder.
            // Wrapping only a direct IID_IGraphBuilder request therefore missed every movie.
            IGraphBuilder* gb = nullptr;
            IUnknown* unknown = static_cast<IUnknown*>(*ppv);
            if (SUCCEEDED(unknown->QueryInterface(IID_IGraphBuilder, reinterpret_cast<void**>(&gb))) && gb) {
                unknown->Release();
                *ppv = static_cast<IGraphBuilder*>(new GraphBuilderProxy(gb));
                gb->Release();
                LogLine("[MOVIE] DirectShow FilterGraph proxy installed");
            }
        }
        return hr;
    }
}

namespace DirectShowHook {
    void SetSkipMovies(bool enable) { gSkipMovies = enable; }
    bool Install() {
        MH_Initialize();
        MH_CreateHookApi(L"ole32.dll", "CoCreateInstance", (LPVOID)Hook_CoCreateInstance, (LPVOID*)&Real_CoCreateInstance);
        MH_CreateHookApi(L"kernel32.dll", "CreateFileA", (LPVOID)Hook_CreateFileA, (LPVOID*)&Real_CreateFileA);
        MH_CreateHookApi(L"kernel32.dll", "CreateFileW", (LPVOID)Hook_CreateFileW, (LPVOID*)&Real_CreateFileW);
        MH_CreateHookApi(L"kernel32.dll", "GetFileAttributesA", (LPVOID)Hook_GetFileAttributesA, (LPVOID*)&Real_GetFileAttributesA);
        MH_CreateHookApi(L"kernel32.dll", "GetFileAttributesW", (LPVOID)Hook_GetFileAttributesW, (LPVOID*)&Real_GetFileAttributesW);
        // In your Install() function:
        MH_CreateHookApi(L"kernel32.dll", "_lopen", (LPVOID)Hook_lopen, (LPVOID*)&Real_lopen);
        MH_CreateHookApi(L"kernel32.dll", "_lcreat", (LPVOID)Hook_lcreat, (LPVOID*)&Real_lcreat);
        MH_EnableHook(MH_ALL_HOOKS);
        return true;
    }
}
