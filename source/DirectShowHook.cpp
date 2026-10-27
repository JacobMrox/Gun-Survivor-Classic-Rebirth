#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <string>
#include <cwctype>
#include <cstring>
#include <cwchar>
#include "MinHook.h"
#include "DirectShowHook.h"
#include "debug.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "strmiids.lib")

namespace
{
    static bool gSkipMovies = false;

    struct GraphState
    {
        bool skipTriggered = false;
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
            HANDLE hEv = NULL;
            if (st->pEvent) st->pEvent->GetEventHandle((OAEVENT*)&hEv);

            if (gSkipMovies) {
                Sleep(150);
            }
            else {
                // Inside KillMovieThread
                while (true) {
                    // Check keys
                    if ((GetAsyncKeyState(VK_SPACE) & 0x8000) || (GetAsyncKeyState(VK_RETURN) & 0x8000)) {
                        if (st->pControl) {
                            st->pControl->Stop();
                            LogLine("[MOVIE] Manual skip triggered by user.");
                            break;
                        }
                    }

                    // Safety exit: if the movie window is closed or the graph is destroyed
                    if (!st->pControl) break;

                    Sleep(15); // 20ms is the sweet spot for input polling
                }
            }

            if (!st->skipTriggered) {
                st->skipTriggered = true;
                IBasicAudio* pBA = nullptr;
                if (SUCCEEDED(st->pGraph->QueryInterface(IID_IBasicAudio, (void**)&pBA))) {
                    pBA->put_Volume(-10000); pBA->Release();
                }
                IMediaSeeking* pMS = nullptr;
                if (SUCCEEDED(st->pGraph->QueryInterface(IID_IMediaSeeking, (void**)&pMS))) {
                    LONGLONG dur = 0;
                    if (SUCCEEDED(pMS->GetDuration(&dur)))
                        pMS->SetPositions(&dur, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
                    pMS->Release();
                }
                if (st->gameHWnd) PostMessageA(st->gameHWnd, 0x8001, (WPARAM)EC_COMPLETE, 0);
            }
            return 0;
        }

        HRESULT STDMETHODCALLTYPE Run() override {
            if (!st->skipTriggered) {
                if (!st->gameHWnd) st->gameHWnd = GetActiveWindow();
                st->pControl = real;
                st->pGraph->QueryInterface(IID_IMediaEvent, (void**)&st->pEvent);
                CreateThread(NULL, 0, KillMovieThread, st, 0, NULL);
            }
            return real->Run();
        }

        // Other methods...
        HRESULT STDMETHODCALLTYPE Pause() override { return real->Pause(); }
        HRESULT STDMETHODCALLTYPE Stop() override { st->skipTriggered = true; return real->Stop(); }
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
        ~GraphBuilderProxy() { if (real) real->Release(); if (st.pEvent) st.pEvent->Release(); }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
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
        HRESULT STDMETHODCALLTYPE RenderFile(LPCWSTR f, LPCWSTR p) override { return real->RenderFile(f, p); }
        HRESULT STDMETHODCALLTYPE AddSourceFilter(LPCWSTR f, LPCWSTR n, IBaseFilter** p) override { return real->AddSourceFilter(f, n, p); }
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
            if (n.find("GBIO.CFG") != std::string::npos) return Real_CreateFileA("NUL", a, s, sa, OPEN_EXISTING, fl, h);
        }
        return Real_CreateFileA(f, a, s, sa, c, fl, h);
    }

    static HANDLE WINAPI Hook_CreateFileW(LPCWSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE h) {
        if (f) {
            std::wstring n = f;
            if (n.find(L"debug.txt") != std::wstring::npos) return Real_CreateFileW(DebugLogPathW().c_str(), a, s, sa, c, fl, h);
            if (n.find(L"GBIO.CFG") != std::wstring::npos) return Real_CreateFileW(L"NUL", a, s, sa, OPEN_EXISTING, fl, h);
        }
        return Real_CreateFileW(f, a, s, sa, c, fl, h);
    }

    static DWORD WINAPI Hook_GetFileAttributesA(LPCSTR f) {
        if (f && std::string(f).find("GBIO.CFG") != std::string::npos) return FILE_ATTRIBUTE_NORMAL;
        return Real_GetFileAttributesA(f);
    }

    static DWORD WINAPI Hook_GetFileAttributesW(LPCWSTR f) {
        if (f && std::wstring(f).find(L"GBIO.CFG") != std::wstring::npos) return FILE_ATTRIBUTE_NORMAL;
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
            if (strstr(lpPathName, "GBIO.CFG")) {
                LogLine("[FILE] _lopen redirecting GBIO.CFG");
                return Real_lopen("NUL", iReadWrite);
            }
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
        if (SUCCEEDED(hr) && IsEqualCLSID(rclsid, CLSID_FilterGraph) && riid == IID_IGraphBuilder) {
            IGraphBuilder* gb = (IGraphBuilder*)*ppv;
            *ppv = new GraphBuilderProxy(gb);
            gb->Release();
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
