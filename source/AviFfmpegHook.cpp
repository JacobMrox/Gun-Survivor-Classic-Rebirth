#include <windows.h>
#include <imagehlp.h>
#include <psapi.h>
#include <vfw.h>
#include <initguid.h>
#include "AviFfmpegHook.h"
#include "globals.h"
#include "debug.h"
#include "survivor_fix/log.h"
#include "survivor_pc.h"
#include <mmsystem.h>
#include <string>
#include <algorithm>

DEFINE_GUID(IID_IAVIFile, 0x00020420, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(IID_IAVIStream, 0x00020421, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

#pragma comment(lib, "imagehlp.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winmm.lib")

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// Global VideoState instance (declared in AviFfmpegHook.h)
VideoState g_Video;

// --- DUMMY CLASSES FOR VFW NEUTRALIZATION ---
class DummyAVIStream {
public:
    virtual STDMETHODIMP QueryInterface(REFIID riid, LPVOID* ppv) { *ppv = nullptr; return E_NOINTERFACE; }
    virtual STDMETHODIMP_(ULONG) AddRef() { return 1; }
    virtual STDMETHODIMP_(ULONG) Release() { return 1; }
    virtual STDMETHODIMP Create(LPARAM lp1, LPARAM lp2) { return E_NOTIMPL; }
    virtual STDMETHODIMP Info(AVISTREAMINFOW* psi, LONG lSize) { if (psi) memset(psi, 0, lSize); return 0; }
    virtual STDMETHODIMP_(LONG) FindSample(LONG lPos, LONG lFlags) { return -1; }
    virtual STDMETHODIMP ReadFormat(LONG lPos, LPVOID lpFormat, LONG* lpcbFormat) { return E_NOTIMPL; }
    virtual STDMETHODIMP SetFormat(LONG lPos, LPVOID lpFormat, LONG cbFormat) { return E_NOTIMPL; }
    virtual STDMETHODIMP Read(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG* plBytes, LONG* plSamples) { return E_NOTIMPL; }
    virtual STDMETHODIMP Write(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, DWORD dwFlags, LONG* plSampWritten, LONG* plBytesWritten) { return E_NOTIMPL; }
    virtual STDMETHODIMP Delete(LONG lStart, LONG lSamples) { return E_NOTIMPL; }
    virtual STDMETHODIMP ReadData(DWORD fcc, LPVOID lp, LONG* lpcb) { return E_NOTIMPL; }
    virtual STDMETHODIMP WriteData(DWORD fcc, LPVOID lp, LONG cb) { return E_NOTIMPL; }
    virtual STDMETHODIMP SetInfo(AVISTREAMINFOW* lpInfo, LONG cbInfo) { return E_NOTIMPL; }
};

class DummyAVIFile {
public:
    virtual STDMETHODIMP QueryInterface(REFIID riid, LPVOID* ppv) {
        if (riid == IID_IAVIFile || riid == IID_IUnknown) { *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    virtual STDMETHODIMP_(ULONG) AddRef() { return 1; }
    virtual STDMETHODIMP_(ULONG) Release() { return 1; }
    virtual STDMETHODIMP Info(AVIFILEINFOW* pfi, LONG lSize) { if (pfi) memset(pfi, 0, lSize); return 0; }
    virtual STDMETHODIMP GetStream(PAVISTREAM* ppStream, DWORD fccType, LONG lParam) {
        static DummyAVIStream s_Stream;
        *ppStream = (IAVIStream*)&s_Stream;
        return S_OK;
    }
    virtual STDMETHODIMP Save(LPCWSTR sz, AVICOMPRESSOPTIONS* lp, AVISAVECALLBACK cb) { return E_NOTIMPL; }
    virtual STDMETHODIMP WriteData(DWORD c, LPVOID d, LONG s) { return E_NOTIMPL; }
    virtual STDMETHODIMP ReadData(DWORD c, LPVOID d, LONG s) { return E_NOTIMPL; }
    virtual STDMETHODIMP EndRecord() { return E_NOTIMPL; }
    virtual STDMETHODIMP DeleteStream(DWORD f, LONG l) { return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE Reserved1() { return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE Reserved2() { return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE Reserved3() { return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE Reserved4() { return E_NOTIMPL; }
    virtual HRESULT STDMETHODCALLTYPE Reserved5() { return E_NOTIMPL; }
};

#ifndef MCI_NOTIFY_SUCCESS
#define MCI_NOTIFY_SUCCESS 0x0001
#endif
#ifndef MM_MCINOTIFY
#define MM_MCINOTIFY 0x03B9
#endif

namespace {
    static DummyAVIFile* g_pDummyFile = nullptr;

    typedef HRESULT(WINAPI* AVIFileOpenA_t)(PAVIFILE*, LPCSTR, UINT, LPCLSID);
    typedef MCIERROR(WINAPI* mciSendStringA_t)(LPCSTR, LPSTR, UINT, HWND);
    typedef HRESULT(WINAPI* CoCreateInstance_t)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
    typedef FARPROC(WINAPI* GetProcAddress_t)(HMODULE, LPCSTR);
    typedef HMODULE(WINAPI* LoadLibraryA_t)(LPCSTR);

    static AVIFileOpenA_t Real_AVIFileOpenA = nullptr;
    static mciSendStringA_t Real_mciSendStringA = nullptr;
    static CoCreateInstance_t Real_CoCreateInstance = nullptr;
    static GetProcAddress_t Real_GetProcAddress = nullptr;
    static LoadLibraryA_t Real_LoadLibraryA = nullptr;
    static thread_local bool g_IsPatching = false;

    void PatchIAT(HMODULE mod);
    bool OpenVideo(const char* filename);
    void CloseVideo();

    // Utility to find filename in MCI string (e.g. 'open "data/movie.mpg" alias x')
    std::string ExtractFileName(const char* cmd) {
        std::string s = cmd;
        size_t first = s.find_first_of("\"");
        size_t last = s.find_last_of("\"");
        if (first != std::string::npos && last != std::string::npos && first < last) {
            return s.substr(first + 1, last - first - 1);
        }
        // Fallback for no quotes
        size_t openPos = s.find("open ");
        if (openPos != std::string::npos) {
            size_t endPos = s.find(" ", openPos + 5);
            return s.substr(openPos + 5, endPos - (openPos + 5));
        }
        return "";
    }

    static HRESULT WINAPI Hook_AVIFileOpenA(PAVIFILE* ppfile, LPCSTR szFile, UINT uMode, LPCLSID lpHandler) {
        if (szFile && (strstr(szFile, ".avi") || strstr(szFile, ".AVI") || strstr(szFile, ".mpg") || strstr(szFile, ".MPG"))) {
            LogLine("AviFfmpegHook: Neutralizing VFW Open for: %s", szFile);
            *ppfile = (PAVIFILE)g_pDummyFile;
            return S_OK;
        }
        return Real_AVIFileOpenA ? Real_AVIFileOpenA(ppfile, szFile, uMode, lpHandler) : AVIERR_ERROR;
    }

    static MCIERROR WINAPI Hook_mciSendStringA(LPCSTR lpstrCommand, LPSTR lpstrReturnString, UINT uReturnLength, HWND hwndCallback) {
        if (lpstrCommand) {
            LogLine("AviFfmpegHook: MCI Command: %s", lpstrCommand);

            if (strstr(lpstrCommand, ".mpg") || strstr(lpstrCommand, ".MPG") ||
                strstr(lpstrCommand, ".avi") || strstr(lpstrCommand, ".AVI")) {

                if (strstr(lpstrCommand, "open")) {
                    std::string moviePath = ExtractFileName(lpstrCommand);
                    LogLine("AviFfmpegHook: Intercepted Movie Path: %s", moviePath.c_str());
                    OpenVideo(moviePath.c_str());
                    return 0;
                }

                if (strstr(lpstrCommand, "status") && lpstrReturnString && uReturnLength > 7) {
                    strcpy_s(lpstrReturnString, uReturnLength, g_Video.isPlaying ? "playing" : "stopped");
                    return 0;
                }

                if (strstr(lpstrCommand, "play")) {
                    if (strstr(lpstrCommand, "notify") && hwndCallback) {
                        PostMessageA(hwndCallback, MM_MCINOTIFY, MCI_NOTIFY_SUCCESS, 0);
                    }
                    return 0;
                }

                if (strstr(lpstrCommand, "close")) {
                    CloseVideo();
                    return 0;
                }
            }
        }
        return Real_mciSendStringA(lpstrCommand, lpstrReturnString, uReturnLength, hwndCallback);
    }

    static HRESULT WINAPI Hook_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID* ppv) {
        // Resident Evil Survivor (gun_bio.exe) plays its movies through DirectShow, not MCI: D3D_Movie.cpp
        // initGraphBuilder creates CLSID_FilterGraph. Blocking it left the opening without text.avi and
        // Opening.avi and the title frozen for ~23 s; DirectShowHook.cpp still honours SkipMovies for it.
        if ((rclsid.Data1 == 0xE436EBB3 || rclsid.Data1 == 0x6B6D0800) && !SurvivorPc_IsSurvivorExe()) {
            LogLine("AviFfmpegHook: Blocking COM Video Object Creation");
            return E_FAIL;
        }
        return Real_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    }

    static HMODULE WINAPI Hook_LoadLibraryA(LPCSTR lpLibFileName) {
        HMODULE hMod = Real_LoadLibraryA(lpLibFileName);
        if (hMod && lpLibFileName && !g_IsPatching) {
            if (strstr(lpLibFileName, "avifil") || strstr(lpLibFileName, "quartz") || strstr(lpLibFileName, "winmm")) {
                PatchIAT(hMod);
            }
        }
        return hMod;
    }

    // IAT entries usually live in read-only memory (.rdata): unprotect the single entry before writing,
    // otherwise the first write is an access violation (0xC0000005 at startup).
    static void SetThunk(PIMAGE_THUNK_DATA thunk, ULONG_PTR fn) {
        DWORD old = 0;
        if (VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), PAGE_READWRITE, &old)) {
            thunk->u1.Function = fn;
            VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), old, &old);
        }
    }

    void PatchIAT(HMODULE mod) {
        if (!mod || mod == GetModuleHandleA("kernel32.dll")) return;
        if (g_IsPatching) return;
        g_IsPatching = true;

        ULONG sz;
        auto* imp = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToData(mod, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &sz);
        if (imp) {
            for (; imp->Name; imp++) {
                // Without OriginalFirstThunk (old bound imports) "orig" would point at the PE header.
                if (!imp->OriginalFirstThunk || !imp->FirstThunk) continue;
                auto* thunk = (PIMAGE_THUNK_DATA)((BYTE*)mod + imp->FirstThunk);
                auto* orig = (PIMAGE_THUNK_DATA)((BYTE*)mod + imp->OriginalFirstThunk);

                for (; orig->u1.AddressOfData; thunk++, orig++) {
                    if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
                    auto* ibn = (PIMAGE_IMPORT_BY_NAME)((BYTE*)mod + orig->u1.AddressOfData);
                    const char* name = (char*)ibn->Name;
                    if (!name) continue;

                    if (strcmp(name, "AVIFileOpenA") == 0) {
                        Real_AVIFileOpenA = (AVIFileOpenA_t)thunk->u1.Function;
                        SetThunk(thunk, (ULONG_PTR)Hook_AVIFileOpenA);
                    }
                    else if (strcmp(name, "mciSendStringA") == 0) {
                        Real_mciSendStringA = (mciSendStringA_t)thunk->u1.Function;
                        SetThunk(thunk, (ULONG_PTR)Hook_mciSendStringA);
                    }
                    else if (strcmp(name, "CoCreateInstance") == 0) {
                        Real_CoCreateInstance = (CoCreateInstance_t)thunk->u1.Function;
                        SetThunk(thunk, (ULONG_PTR)Hook_CoCreateInstance);
                    }
                }
            }
        }
        g_IsPatching = false;
    }

    bool OpenVideo(const char* filename) {
        CloseVideo(); // Reset any existing state
        if (avformat_open_input(&g_Video.pFormatCtx, filename, NULL, NULL) != 0) return false;
        if (avformat_find_stream_info(g_Video.pFormatCtx, NULL) < 0) return false;

        for (int i = 0; i < (int)g_Video.pFormatCtx->nb_streams; i++) {
            if (g_Video.pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                g_Video.videoStream = i;
                break;
            }
        }

        if (g_Video.videoStream == -1) return false;

        AVCodecParameters* pCodecPar = g_Video.pFormatCtx->streams[g_Video.videoStream]->codecpar;
        const AVCodec* pCodec = avcodec_find_decoder(pCodecPar->codec_id);
        g_Video.pCodecCtx = avcodec_alloc_context3(pCodec);
        avcodec_parameters_to_context(g_Video.pCodecCtx, pCodecPar);
        avcodec_open2(g_Video.pCodecCtx, pCodec, NULL);

        g_Video.pFrame = av_frame_alloc();
        g_Video.pFrameRGB = av_frame_alloc();

        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32, g_Video.pCodecCtx->width, g_Video.pCodecCtx->height, 1);
        g_Video.buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

        av_image_fill_arrays(g_Video.pFrameRGB->data, g_Video.pFrameRGB->linesize, g_Video.buffer,
            AV_PIX_FMT_RGB32, g_Video.pCodecCtx->width, g_Video.pCodecCtx->height, 1);

        g_Video.sws_ctx = sws_getContext(g_Video.pCodecCtx->width, g_Video.pCodecCtx->height, g_Video.pCodecCtx->pix_fmt,
            g_Video.pCodecCtx->width, g_Video.pCodecCtx->height, AV_PIX_FMT_RGB32,
            SWS_BILINEAR, NULL, NULL, NULL);

        AVRational rate = g_Video.pFormatCtx->streams[g_Video.videoStream]->avg_frame_rate;
        if (rate.num <= 0 || rate.den <= 0)
            rate = g_Video.pFormatCtx->streams[g_Video.videoStream]->r_frame_rate;
        const double fps = (rate.num > 0 && rate.den > 0) ? av_q2d(rate) : 30.0;
        g_Video.frameIntervalMs = (fps > 1.0 && fps < 240.0) ? (1000.0 / fps) : (1000.0 / 30.0);
        g_Video.playbackStartedAtMs = GetTickCount64();
        g_Video.nextFrameAtMs = 0.0;
        g_Video.eof = false;
        g_Video.resetTexture = true;
        g_Video.isPlaying = true;
        LogLine("AviFfmpegHook: FFmpeg display started for %dx%d video at %.3f fps: %s",
            g_Video.pCodecCtx->width, g_Video.pCodecCtx->height, fps, filename);
        return true;
    }

    void CloseVideo() {
        g_Video.isPlaying = false;
        if (g_Video.sws_ctx) sws_freeContext(g_Video.sws_ctx);
        if (g_Video.buffer) av_free(g_Video.buffer);
        if (g_Video.pFrameRGB) av_frame_free(&g_Video.pFrameRGB);
        if (g_Video.pFrame) av_frame_free(&g_Video.pFrame);
        if (g_Video.pCodecCtx) avcodec_free_context(&g_Video.pCodecCtx);
        if (g_Video.pFormatCtx) avformat_close_input(&g_Video.pFormatCtx);

        g_Video.sws_ctx = nullptr;
        g_Video.buffer = nullptr;
        g_Video.pFrameRGB = nullptr;
        g_Video.pFrame = nullptr;
        g_Video.pCodecCtx = nullptr;
        g_Video.pFormatCtx = nullptr;
        g_Video.videoStream = -1;
        g_Video.eof = false;
        g_Video.nextFrameAtMs = 0.0;
    }
}

namespace AviFfmpegHook {
    static volatile LONG g_directShowGeneration = 0;
    static volatile LONG g_activeDirectShowGeneration = 0;

    struct OverlayState {
        std::string path;
        HWND parent = nullptr;
        volatile LONG stop = FALSE;
        unsigned long generation = 0;
        uint8_t* pixels = nullptr;
        BITMAPINFO info{};
        int sourceWidth = 0;
        int sourceHeight = 0;
    };
    static OverlayState* g_overlay = nullptr;
    static volatile LONG g_overlayGeneration = 0;

    static LRESULT CALLBACK OverlayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        // A STATIC/SS_BLACKRECT child erases itself to black whenever Windows
        // repaints it, briefly replacing the FFmpeg frame. The movie thread
        // owns all frame drawing, so suppress background erasure and empty
        // paint requests instead of flashing black between decoded frames.
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            auto* state = reinterpret_cast<OverlayState*>(GetWindowLongPtrA(window, GWLP_USERDATA));
            if (state && state->pixels && state->sourceWidth > 0 && state->sourceHeight > 0) {
                RECT rc{};
                GetClientRect(window, &rc);
                StretchDIBits(dc, 0, 0, rc.right, rc.bottom, 0, 0,
                    state->sourceWidth, state->sourceHeight, state->pixels,
                    &state->info, DIB_RGB_COLORS, SRCCOPY);
            }
            EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcA(window, message, wParam, lParam);
    }

    static bool EnsureOverlayWindowClass() {
        static bool overlayClassAvailable = []() {
            WNDCLASSEXA wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = OverlayWindowProc;
            wc.hInstance = GetModuleHandleA(nullptr);
            wc.lpszClassName = "SurvivorFfmpegOverlay";
            return RegisterClassExA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }();
        return overlayClassAvailable;
    }

    static BOOL CALLBACK HideNativeMovieChild(HWND window, LPARAM overlayValue) {
        const HWND overlay = reinterpret_cast<HWND>(overlayValue);
        if (window != overlay && IsWindowVisible(window)) ShowWindow(window, SW_HIDE);
        return TRUE;
    }

    static void PresentOverlayFrame(OverlayState* state, HWND overlay) {
        if (!state || !overlay || !state->parent || !state->pixels) return;
        // Direct3D presents and legacy renderer state changes can cover a GDI
        // child between source-video frames. Keep the replacement above every
        // native child and repaint the held FFmpeg frame without erasing it.
        EnumChildWindows(state->parent, HideNativeMovieChild, reinterpret_cast<LPARAM>(overlay));
        RECT client{};
        GetClientRect(state->parent, &client);
        POINT origin{client.left, client.top};
        ClientToScreen(state->parent, &origin);
        SetWindowPos(overlay, HWND_TOP, origin.x, origin.y,
            client.right - client.left, client.bottom - client.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawWindow(overlay, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }

    static DWORD WINAPI OverlayThread(LPVOID argument) {
        auto* state = static_cast<OverlayState*>(argument);
        AVFormatContext* format = nullptr;
        AVCodecContext* codec = nullptr;
        AVFrame* frame = nullptr;
        AVFrame* rgb = nullptr;
        SwsContext* scale = nullptr;
        uint8_t* pixels = nullptr;
        HWND overlay = nullptr;
        int stream = -1, size = 0;
        const AVCodec* decoder = nullptr;
        BITMAPINFO info{};
        AVRational rate{};
        DWORD delay = 33;
        int lastClientW = 0, lastClientH = 0;
        int64_t firstPts = AV_NOPTS_VALUE;
        uint64_t frameNumber = 0;
        ULONGLONG playbackStart = 0;

        if (avformat_open_input(&format, state->path.c_str(), nullptr, nullptr) != 0 || avformat_find_stream_info(format, nullptr) < 0) goto done;
        for (unsigned i = 0; i < format->nb_streams; ++i) if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { stream = static_cast<int>(i); break; }
        if (stream < 0) goto done;
        decoder = avcodec_find_decoder(format->streams[stream]->codecpar->codec_id);
        if (!decoder || !(codec = avcodec_alloc_context3(decoder)) || avcodec_parameters_to_context(codec, format->streams[stream]->codecpar) < 0 || avcodec_open2(codec, decoder, nullptr) < 0) goto done;
        if (!(frame = av_frame_alloc()) || !(rgb = av_frame_alloc())) goto done;
        size = av_image_get_buffer_size(AV_PIX_FMT_BGRA, codec->width, codec->height, 1);
        if (size <= 0 || !(pixels = static_cast<uint8_t*>(av_malloc(size)))) goto done;
        av_image_fill_arrays(rgb->data, rgb->linesize, pixels, AV_PIX_FMT_BGRA, codec->width, codec->height, 1);
        if (!(scale = sws_getContext(codec->width, codec->height, codec->pix_fmt, codec->width, codec->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr))) goto done;
        if (!state->parent) state->parent = GetForegroundWindow();
        if (!state->parent) goto done;
        if (!EnsureOverlayWindowClass()) goto done;
        // A child HWND shares the game's presentation hierarchy and can be
        // covered by a Direct3D/native-video present. An owned layered popup
        // has its own compositor surface while still following/minimizing with
        // the game window, so its last decoded frame cannot flicker to black.
        overlay = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
            "SurvivorFfmpegOverlay", "", WS_POPUP, 0, 0, 1, 1,
            state->parent, nullptr, GetModuleHandleA(nullptr), nullptr);
        if (!overlay) goto done;
        SetLayeredWindowAttributes(overlay, 0, 255, LWA_ALPHA);
        SetWindowLongPtrA(overlay, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = codec->width; info.bmiHeader.biHeight = -codec->height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        state->pixels = pixels;
        state->info = info;
        state->sourceWidth = codec->width;
        state->sourceHeight = codec->height;
        rate = format->streams[stream]->avg_frame_rate;
        delay = rate.num > 0 && rate.den > 0 ? static_cast<DWORD>(1000.0 * rate.den / rate.num + 0.5) : 33;
        playbackStart = GetTickCount64();
        sf::Log("FFmpeg overlay started: %s", state->path.c_str());
        while (!InterlockedCompareExchange(&state->stop, FALSE, FALSE)) {
            AVPacket packet{}; bool got = false;
            while (!got && !InterlockedCompareExchange(&state->stop, FALSE, FALSE)) {
                if (av_read_frame(format, &packet) < 0) { InterlockedExchange(&state->stop, TRUE); break; }
                if (packet.stream_index == stream) { avcodec_send_packet(codec, &packet); got = avcodec_receive_frame(codec, frame) == 0; }
                av_packet_unref(&packet);
            }
            if (!got) break;
            const int64_t pts = frame->best_effort_timestamp;
            if (firstPts == AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE) firstPts = pts;
            double targetMs = static_cast<double>(frameNumber) * delay;
            if (pts != AV_NOPTS_VALUE && firstPts != AV_NOPTS_VALUE) {
                targetMs = static_cast<double>(pts - firstPts) *
                    av_q2d(format->streams[stream]->time_base) * 1000.0;
                if (targetMs < 0.0) targetMs = 0.0;
            }
            ++frameNumber;

            // Pace from the absolute stream timestamp. Decode/conversion and
            // window-management time no longer accumulates once per frame.
            // If decoding falls substantially behind audio, discard the late
            // video frame and catch up to the next timestamp.
            double elapsedMs = static_cast<double>(GetTickCount64() - playbackStart);
            const double lateLimit = (std::max)(80.0, static_cast<double>(delay) * 2.0);
            if (elapsedMs > targetMs + lateLimit) continue;
            DWORD nextHeldFrameRefresh = GetTickCount() + 8;
            while (!InterlockedCompareExchange(&state->stop, FALSE, FALSE) && elapsedMs + 0.5 < targetMs) {
                MSG message{};
                while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageA(&message);
                }
                const DWORD now = GetTickCount();
                if (state->pixels && static_cast<LONG>(now - nextHeldFrameRefresh) >= 0) {
                    PresentOverlayFrame(state, overlay);
                    nextHeldFrameRefresh = now + 8;
                }
                Sleep(1);
                elapsedMs = static_cast<double>(GetTickCount64() - playbackStart);
            }
            if (InterlockedCompareExchange(&state->stop, FALSE, FALSE)) break;
            sws_scale(scale, frame->data, frame->linesize, 0, codec->height, rgb->data, rgb->linesize);
            RECT rc{}; GetClientRect(state->parent, &rc);
            if (rc.right != lastClientW || rc.bottom != lastClientH) {
                lastClientW = rc.right; lastClientH = rc.bottom;
            }
            PresentOverlayFrame(state, overlay);
            // Keep native audio and completion handling, but retire the old video
            // surface only after this decoded FFmpeg frame is actually visible.
            EnumChildWindows(state->parent, HideNativeMovieChild, reinterpret_cast<LPARAM>(overlay));
        }
    done:
        state->pixels = nullptr;
        if (overlay) { SetWindowLongPtrA(overlay, GWLP_USERDATA, 0); DestroyWindow(overlay); }
        if (scale) sws_freeContext(scale); if (pixels) av_free(pixels); if (rgb) av_frame_free(&rgb); if (frame) av_frame_free(&frame); if (codec) avcodec_free_context(&codec); if (format) avformat_close_input(&format);
        if (g_overlay == state) g_overlay = nullptr; delete state; return 0;
    }

    unsigned long StartForGameOverlay(const char* filename) {
        if (!filename || !*filename) return 0;
        StopForGameOverlay();
        auto* state = new OverlayState{};
        state->path = filename;
        state->parent = GetActiveWindow();
        state->generation = static_cast<unsigned long>(InterlockedIncrement(&g_overlayGeneration));
        g_overlay = state;
        if (!CreateThread(nullptr, 0, OverlayThread, state, 0, nullptr)) { g_overlay = nullptr; delete state; return 0; }
        return state->generation;
    }

    unsigned long StartForGameOverlay(const wchar_t* filename) {
        if (!filename || !*filename) return 0;
        char path[MAX_PATH * 4] = {};
        if (!WideCharToMultiByte(CP_ACP, 0, filename, -1, path, sizeof(path), nullptr, nullptr)) return 0;
        return StartForGameOverlay(path);
    }

    void StopForGameOverlay(unsigned long generation) {
        OverlayState* state = g_overlay;
        if (state && (!generation || state->generation == generation)) InterlockedExchange(&state->stop, TRUE);
    }
    bool IsGameOverlayActive(unsigned long generation) {
        OverlayState* state = g_overlay;
        return state && (!generation || state->generation == generation);
    }

    bool Install() {
        static DummyAVIFile s_DummyFile;
        g_pDummyFile = &s_DummyFile;
        HMODULE hMods[1024]; DWORD cb;
        if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cb)) {
            for (unsigned int i = 0; i < (cb / sizeof(HMODULE)); i++) PatchIAT(hMods[i]);
        }
        return true;
    }

    unsigned long StartForDirectShow(const wchar_t* filename) {
        if (!filename || !*filename) return 0;
        char path[MAX_PATH * 4] = {};
        if (!WideCharToMultiByte(CP_ACP, 0, filename, -1, path, sizeof(path), nullptr, nullptr)) {
            LogLine("AviFfmpegHook: could not convert DirectShow movie path");
            return 0;
        }
        const LONG generation = InterlockedIncrement(&g_directShowGeneration);
        // Claim ownership before OpenVideo closes the previous decoder. A late
        // Stop from the previous graph will now see a different generation.
        InterlockedExchange(&g_activeDirectShowGeneration, generation);
        if (!OpenVideo(path)) {
            InterlockedCompareExchange(&g_activeDirectShowGeneration, 0, generation);
            return 0;
        }
        return static_cast<unsigned long>(generation);
    }

    void StopForDirectShow(unsigned long generation) {
        const LONG active = InterlockedCompareExchange(&g_activeDirectShowGeneration, 0, 0);
        if (generation && active != static_cast<LONG>(generation)) return;
        if (generation)
            InterlockedCompareExchange(&g_activeDirectShowGeneration, 0, static_cast<LONG>(generation));
        else
            InterlockedExchange(&g_activeDirectShowGeneration, 0);
        if (g_Video.isPlaying || g_Video.pFormatCtx) {
            LogLine("AviFfmpegHook: FFmpeg display stopped");
            CloseVideo();
        }
    }

    bool IsDirectShowActive(unsigned long generation) {
        return generation != 0 && g_Video.isPlaying &&
            InterlockedCompareExchange(&g_activeDirectShowGeneration, 0, 0) == static_cast<LONG>(generation);
    }
}
