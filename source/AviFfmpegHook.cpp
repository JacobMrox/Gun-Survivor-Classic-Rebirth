#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <imagehlp.h>
#include <vfw.h>

#include <string>
#include <vector>
#include <memory>

#pragma comment(lib, "imagehlp.lib")

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include "AviFfmpegHook.h"
#include "debug.h"


// Log IAT patch result
static void LogPatch(const char* name, bool ok)
{
    LogLine("[AviFfmpegHook] IAT patch %s: %s", name, ok ? "OK" : "NOT FOUND");
}

namespace
{
    // ------------------------------
    // Simple IAT patcher
    // ------------------------------
    bool PatchIAT(HMODULE module, const char* importedDll, const char* funcName, void* hookFn, void** realFnOut)
    {
        ULONG size = 0;
        auto* impDesc = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToData(
            module, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &size);

        if (!impDesc) return false;

        for (; impDesc->Name; impDesc++)
        {
            const char* dllName = (const char*)((BYTE*)module + impDesc->Name);
            if (_stricmp(dllName, importedDll) != 0) continue;

            auto* thunk = (PIMAGE_THUNK_DATA)((BYTE*)module + impDesc->FirstThunk);
            auto* orig = (PIMAGE_THUNK_DATA)((BYTE*)module + impDesc->OriginalFirstThunk);

            for (; orig && orig->u1.AddressOfData; thunk++, orig++)
            {
                if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;

                auto* ibn = (PIMAGE_IMPORT_BY_NAME)((BYTE*)module + orig->u1.AddressOfData);
                if (!ibn || !ibn->Name) continue;

                if (strcmp((char*)ibn->Name, funcName) != 0) continue;

                DWORD oldProt = 0;
                if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt))
                    return false;

                if (realFnOut) *realFnOut = (void*)thunk->u1.Function;
                thunk->u1.Function = (ULONG_PTR)hookFn;

                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProt, &oldProt);
                return true;
            }
        }
        return false;
    }

    std::wstring AnsiToWide(LPCSTR s)
    {
        if (!s) return L"";
        int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        std::wstring w(n > 1 ? n - 1 : 0, L'\0');
        if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
        return w;
    }

    bool HasMovieExt(const std::wstring& p)
    {
        auto lower = p;
        for (auto& ch : lower) ch = (wchar_t)towlower(ch);

        // add more if you want
        return lower.rfind(L".avi") != std::wstring::npos ||
            lower.rfind(L".mpg") != std::wstring::npos ||
            lower.rfind(L".mpeg") != std::wstring::npos ||
            lower.rfind(L".mp4") != std::wstring::npos;
    }

    // ------------------------------
    // FFmpeg-backed "movie" object
    // Emulates AVIStreamGetFrame path (returns a DIB: BITMAPINFOHEADER + BGR24 pixels)
    // ------------------------------
    struct FFMpegMovie
    {
        AVFormatContext* fmt = nullptr;
        AVCodecContext* vdec = nullptr;
        SwsContext* sws = nullptr;
        int videoStream = -1;

        int width = 0;
        int height = 0;
        AVRational timeBase{};
        double fps = 30.0;
        int64_t frameCount = 0;

        AVFrame* frame = nullptr;
        AVPacket* pkt = nullptr;

        // buffer returned by AVIStreamGetFrame -> pointer stays valid until next GetFrame
        std::vector<uint8_t> dibBuffer;

        ~FFMpegMovie()
        {
            if (sws) sws_freeContext(sws);
            if (vdec) avcodec_free_context(&vdec);
            if (fmt) avformat_close_input(&fmt);
            if (frame) av_frame_free(&frame);
            if (pkt) av_packet_free(&pkt);
        }

        bool openFile(const wchar_t* pathW)
        {
            int needed = WideCharToMultiByte(CP_UTF8, 0, pathW, -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 0) return false;

            std::string pathUtf8(needed, '\0');
            WideCharToMultiByte(CP_UTF8, 0, pathW, -1, pathUtf8.data(), needed, nullptr, nullptr);

            if (avformat_open_input(&fmt, pathUtf8.c_str(), nullptr, nullptr) < 0) return false;
            if (avformat_find_stream_info(fmt, nullptr) < 0) return false;

            videoStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (videoStream < 0) return false;

            AVStream* vs = fmt->streams[videoStream];
            width = vs->codecpar->width;
            height = vs->codecpar->height;
            timeBase = vs->time_base;

            AVRational fr = vs->avg_frame_rate.num ? vs->avg_frame_rate : vs->r_frame_rate;
            if (fr.num && fr.den)
            {
                double f = (double)fr.num / (double)fr.den;
                if (f > 1.0 && f < 240.0) fps = f;
            }

            if (vs->nb_frames > 0) frameCount = (int64_t)vs->nb_frames;
            else if (fmt->duration > 0 && fps > 0.0)
                frameCount = (int64_t)((fmt->duration / (double)AV_TIME_BASE) * fps);

            const AVCodec* codec = avcodec_find_decoder(vs->codecpar->codec_id);
            if (!codec) return false;

            vdec = avcodec_alloc_context3(codec);
            if (!vdec) return false;

            if (avcodec_parameters_to_context(vdec, vs->codecpar) < 0) return false;
            if (avcodec_open2(vdec, codec, nullptr) < 0) return false;

            frame = av_frame_alloc();
            pkt = av_packet_alloc();
            if (!frame || !pkt) return false;

            sws = sws_getContext(width, height, vdec->pix_fmt,
                width, height, AV_PIX_FMT_BGR24,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws) return false;

            return true;
        }

        int64_t frameIndexToTimestamp(int64_t frameIndex) const
        {
            // seconds = frameIndex / fps; ts = seconds / timeBase
            double seconds = (fps > 0.0) ? (double)frameIndex / fps : 0.0;
            double tb = av_q2d(timeBase);
            if (tb <= 0.0) tb = 1.0 / 90000.0;
            return (int64_t)(seconds / tb);
        }

        bool seekToFrame(int64_t frameIndex)
        {
            if (!fmt || videoStream < 0) return false;
            int64_t ts = frameIndexToTimestamp(frameIndex);
            if (av_seek_frame(fmt, videoStream, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
            avcodec_flush_buffers(vdec);
            return true;
        }

        BITMAPINFOHEADER* decodeFrameToDIB(int64_t requestedFrame)
        {
            if (!seekToFrame(requestedFrame)) return nullptr;

            bool got = false;

            while (!got)
            {
                int rr = av_read_frame(fmt, pkt);
                if (rr < 0)
                {
                    // flush
                    avcodec_send_packet(vdec, nullptr);
                }
                else
                {
                    if (pkt->stream_index != videoStream)
                    {
                        av_packet_unref(pkt);
                        continue;
                    }
                    if (avcodec_send_packet(vdec, pkt) < 0)
                    {
                        av_packet_unref(pkt);
                        continue;
                    }
                    av_packet_unref(pkt);
                }

                while (true)
                {
                    rr = avcodec_receive_frame(vdec, frame);
                    if (rr == AVERROR(EAGAIN) || rr == AVERROR_EOF) break;
                    if (rr < 0) return nullptr;
                    got = true;
                    break;
                }
            }

            if (!got) return nullptr;

            const int stride = width * 3;
            const int imgSize = stride * height;

            std::vector<uint8_t> bgr(imgSize);

            uint8_t* dstData[4] = { bgr.data(), nullptr, nullptr, nullptr };
            int dstLinesize[4] = { stride, 0, 0, 0 };

            sws_scale(sws, frame->data, frame->linesize, 0, height, dstData, dstLinesize);

            dibBuffer.resize(sizeof(BITMAPINFOHEADER) + imgSize);
            auto* bih = reinterpret_cast<BITMAPINFOHEADER*>(dibBuffer.data());
            ZeroMemory(bih, sizeof(BITMAPINFOHEADER));
            bih->biSize = sizeof(BITMAPINFOHEADER);
            bih->biWidth = width;
            bih->biHeight = height;      // bottom-up
            bih->biPlanes = 1;
            bih->biBitCount = 24;
            bih->biCompression = BI_RGB;
            bih->biSizeImage = imgSize;

            uint8_t* outPixels = dibBuffer.data() + sizeof(BITMAPINFOHEADER);

            // flip vertically for bottom-up DIB
            for (int y = 0; y < height; y++)
            {
                const uint8_t* srcRow = bgr.data() + (height - 1 - y) * stride;
                uint8_t* dstRow = outPixels + y * stride;
                memcpy(dstRow, srcRow, stride);
            }

            return bih;
        }
    };

    // ------------------------------
    // Fake handles (opaque pointers)
    // ------------------------------
    struct FakeAviFile { std::shared_ptr<FFMpegMovie> movie; std::wstring path; };
    struct FakeAviStream { std::shared_ptr<FFMpegMovie> movie; };
    struct FakeGetFrame { std::shared_ptr<FFMpegMovie> movie; };

    // ------------------------------
    // Real AVIFIL32 functions (for fallback)
    // ------------------------------
    using AVIFileOpenW_t = HRESULT(WINAPI*)(PAVIFILE* ppfile, LPCWSTR szFile, UINT uMode, LPCLSID lpHandler);
    using AVIFileOpenA_t = HRESULT(WINAPI*)(PAVIFILE* ppfile, LPCSTR  szFile, UINT uMode, LPCLSID lpHandler);
    using AVIFileRelease_t = ULONG(WINAPI*)(PAVIFILE pfile);
    using AVIFileGetStream_t = HRESULT(WINAPI*)(PAVIFILE pfile, PAVISTREAM* ppStream, DWORD fccType, LONG lParam);

    using AVIStreamInfoW_t = HRESULT(WINAPI*)(PAVISTREAM pStream, AVISTREAMINFOW* psi, LONG lSize);
    using AVIStreamInfoA_t = HRESULT(WINAPI*)(PAVISTREAM pStream, AVISTREAMINFOA* psi, LONG lSize);
    using AVIStreamLength_t = LONG(WINAPI*)(PAVISTREAM pStream);
    using AVIStreamSampleToTime_t = LONG(WINAPI*)(PAVISTREAM pStream, LONG lSample);

    using AVIStreamGetFrameOpen_t = PGETFRAME(WINAPI*)(PAVISTREAM pStream, LPBITMAPINFOHEADER lpbiWanted);
    using AVIStreamGetFrame_t = LPVOID(WINAPI*)(PGETFRAME pg, LONG lPos);
    using AVIStreamGetFrameClose_t = HRESULT(WINAPI*)(PGETFRAME pg);

    using AVIStreamRelease_t = ULONG(WINAPI*)(PAVISTREAM pStream);

    static AVIFileOpenW_t           Real_AVIFileOpenW = nullptr;
    static AVIFileOpenA_t           Real_AVIFileOpenA = nullptr;
    static AVIFileRelease_t         Real_AVIFileRelease = nullptr;
    static AVIFileGetStream_t       Real_AVIFileGetStream = nullptr;

    static AVIStreamInfoW_t         Real_AVIStreamInfoW = nullptr;
    static AVIStreamInfoA_t         Real_AVIStreamInfoA = nullptr;
    static AVIStreamLength_t        Real_AVIStreamLength = nullptr;
    static AVIStreamSampleToTime_t  Real_AVIStreamSampleToTime = nullptr;

    static AVIStreamGetFrameOpen_t  Real_AVIStreamGetFrameOpen = nullptr;
    static AVIStreamGetFrame_t      Real_AVIStreamGetFrame = nullptr;
    static AVIStreamGetFrameClose_t Real_AVIStreamGetFrameClose = nullptr;

    static AVIStreamRelease_t       Real_AVIStreamRelease = nullptr;

    // ------------------------------
    // Hooked implementations
    // ------------------------------
    HRESULT WINAPI Hook_AVIFileOpenW(PAVIFILE* ppfile, LPCWSTR szFile, UINT uMode, LPCLSID lpHandler)
    {
        if (!ppfile) return AVIERR_BADPARAM;
        *ppfile = nullptr;

        if (szFile && HasMovieExt(szFile))
        {
            auto movie = std::make_shared<FFMpegMovie>();
            if (movie->openFile(szFile))
            {
                auto* f = new FakeAviFile();
                f->movie = movie;
                f->path = szFile;
                *ppfile = reinterpret_cast<PAVIFILE>(f);
                return S_OK;
            }
        }

        return Real_AVIFileOpenW ? Real_AVIFileOpenW(ppfile, szFile, uMode, lpHandler) : AVIERR_ERROR;
    }

    HRESULT WINAPI Hook_AVIFileOpenA(PAVIFILE* ppfile, LPCSTR szFile, UINT uMode, LPCLSID lpHandler)
    {
        if (!ppfile) return AVIERR_BADPARAM;
        *ppfile = nullptr;

        if (szFile)
        {
            std::wstring w = AnsiToWide(szFile);
            if (HasMovieExt(w))
            {
                auto movie = std::make_shared<FFMpegMovie>();
                if (movie->openFile(w.c_str()))
                {
                    auto* f = new FakeAviFile();
                    f->movie = movie;
                    f->path = w;
                    *ppfile = reinterpret_cast<PAVIFILE>(f);
                    return S_OK;
                }
            }
        }

        return Real_AVIFileOpenA ? Real_AVIFileOpenA(ppfile, szFile, uMode, lpHandler) : AVIERR_ERROR;
    }

    ULONG WINAPI Hook_AVIFileRelease(PAVIFILE pfile)
    {
        // Safe-ish: only delete if it looks like our fake type (has movie shared_ptr)
        auto* f = reinterpret_cast<FakeAviFile*>(pfile);
        __try
        {
            if (f && f->movie)
            {
                delete f;
                return 0;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIFileRelease ? Real_AVIFileRelease(pfile) : 0;
    }

    HRESULT WINAPI Hook_AVIFileGetStream(PAVIFILE pfile, PAVISTREAM* ppStream, DWORD fccType, LONG lParam)
    {
        if (!ppStream) return AVIERR_BADPARAM;
        *ppStream = nullptr;

        auto* f = reinterpret_cast<FakeAviFile*>(pfile);
        __try
        {
            if (f && f->movie)
            {
                // Many games request streamtypeVIDEO, or pass 0 to mean "first".
                if (fccType == streamtypeVIDEO || fccType == 0)
                {
                    auto* s = new FakeAviStream();
                    s->movie = f->movie;
                    *ppStream = reinterpret_cast<PAVISTREAM>(s);
                    return S_OK;
                }
                return AVIERR_NODATA;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIFileGetStream ? Real_AVIFileGetStream(pfile, ppStream, fccType, lParam) : AVIERR_NODATA;
    }

    ULONG WINAPI Hook_AVIStreamRelease(PAVISTREAM pStream)
    {
        auto* s = reinterpret_cast<FakeAviStream*>(pStream);
        __try
        {
            if (s && s->movie)
            {
                delete s;
                return 0;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIStreamRelease ? Real_AVIStreamRelease(pStream) : 0;
    }

    HRESULT WINAPI Hook_AVIStreamInfoW(PAVISTREAM pStream, AVISTREAMINFOW* psi, LONG lSize)
    {
        auto* s = reinterpret_cast<FakeAviStream*>(pStream);
        __try
        {
            if (s && s->movie && psi && lSize >= (LONG)sizeof(AVISTREAMINFOW))
            {
                ZeroMemory(psi, sizeof(AVISTREAMINFOW));
                psi->fccType = streamtypeVIDEO;
                psi->dwScale = 1;
                psi->dwRate = (DWORD)(s->movie->fps > 0 ? (s->movie->fps + 0.5) : 30);
                psi->dwLength = (DWORD)(s->movie->frameCount > 0 ? s->movie->frameCount : 0x7fffffff);
                psi->rcFrame.left = 0;
                psi->rcFrame.top = 0;
                psi->rcFrame.right = s->movie->width;
                psi->rcFrame.bottom = s->movie->height;
                return S_OK;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIStreamInfoW ? Real_AVIStreamInfoW(pStream, psi, lSize) : AVIERR_ERROR;
    }

    HRESULT WINAPI Hook_AVIStreamInfoA(PAVISTREAM pStream, AVISTREAMINFOA* psi, LONG lSize)
    {
        auto* s = reinterpret_cast<FakeAviStream*>(pStream);
        __try
        {
            if (s && s->movie && psi && lSize >= (LONG)sizeof(AVISTREAMINFOA))
            {
                ZeroMemory(psi, sizeof(AVISTREAMINFOA));
                psi->fccType = streamtypeVIDEO;
                psi->dwScale = 1;
                psi->dwRate = (DWORD)(s->movie->fps > 0 ? (s->movie->fps + 0.5) : 30);
                psi->dwLength = (DWORD)(s->movie->frameCount > 0 ? s->movie->frameCount : 0x7fffffff);
                psi->rcFrame.left = 0;
                psi->rcFrame.top = 0;
                psi->rcFrame.right = s->movie->width;
                psi->rcFrame.bottom = s->movie->height;
                return S_OK;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIStreamInfoA ? Real_AVIStreamInfoA(pStream, psi, lSize) : AVIERR_ERROR;
    }

    LONG WINAPI Hook_AVIStreamLength(PAVISTREAM pStream)
    {
        auto* s = reinterpret_cast<FakeAviStream*>(pStream);
        __try
        {
            if (s && s->movie)
            {
                if (s->movie->frameCount > 0) return (LONG)s->movie->frameCount;
                return 0x7fffffff;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIStreamLength ? Real_AVIStreamLength(pStream) : 0;
    }

    LONG WINAPI Hook_AVIStreamSampleToTime(PAVISTREAM pStream, LONG lSample)
    {
        auto* s = reinterpret_cast<FakeAviStream*>(pStream);
        __try
        {
            if (s && s->movie && s->movie->fps > 0.0)
            {
                double ms = (double)lSample * (1000.0 / s->movie->fps);
                return (LONG)(ms + 0.5);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIStreamSampleToTime ? Real_AVIStreamSampleToTime(pStream, lSample) : 0;
    }

    PGETFRAME WINAPI Hook_AVIStreamGetFrameOpen(PAVISTREAM pStream, LPBITMAPINFOHEADER /*lpbiWanted*/)
    {
        auto* s = reinterpret_cast<FakeAviStream*>(pStream);
        __try
        {
            if (s && s->movie)
            {
                auto* gf = new FakeGetFrame();
                gf->movie = s->movie;
                return reinterpret_cast<PGETFRAME>(gf);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIStreamGetFrameOpen ? Real_AVIStreamGetFrameOpen(pStream, nullptr) : nullptr;
    }

    LPVOID WINAPI Hook_AVIStreamGetFrame(PGETFRAME pg, LONG lPos)
    {
        auto* gf = reinterpret_cast<FakeGetFrame*>(pg);
        __try
        {
            if (gf && gf->movie)
            {
                return gf->movie->decodeFrameToDIB((int64_t)lPos);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIStreamGetFrame ? Real_AVIStreamGetFrame(pg, lPos) : nullptr;
    }

    HRESULT WINAPI Hook_AVIStreamGetFrameClose(PGETFRAME pg)
    {
        auto* gf = reinterpret_cast<FakeGetFrame*>(pg);
        __try
        {
            if (gf && gf->movie)
            {
                delete gf;
                return S_OK;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        return Real_AVIStreamGetFrameClose ? Real_AVIStreamGetFrameClose(pg) : AVIERR_ERROR;
    }

    bool ResolveRealAvifil32()
    {
        HMODULE avifil = LoadLibraryW(L"avifil32.dll");
        if (!avifil) return false;

        Real_AVIFileOpenW = (AVIFileOpenW_t)GetProcAddress(avifil, "AVIFileOpenW");
        Real_AVIFileOpenA = (AVIFileOpenA_t)GetProcAddress(avifil, "AVIFileOpenA");
        Real_AVIFileRelease = (AVIFileRelease_t)GetProcAddress(avifil, "AVIFileRelease");
        Real_AVIFileGetStream = (AVIFileGetStream_t)GetProcAddress(avifil, "AVIFileGetStream");

        Real_AVIStreamInfoW = (AVIStreamInfoW_t)GetProcAddress(avifil, "AVIStreamInfoW");
        Real_AVIStreamInfoA = (AVIStreamInfoA_t)GetProcAddress(avifil, "AVIStreamInfoA");
        Real_AVIStreamLength = (AVIStreamLength_t)GetProcAddress(avifil, "AVIStreamLength");
        Real_AVIStreamSampleToTime = (AVIStreamSampleToTime_t)GetProcAddress(avifil, "AVIStreamSampleToTime");

        Real_AVIStreamGetFrameOpen = (AVIStreamGetFrameOpen_t)GetProcAddress(avifil, "AVIStreamGetFrameOpen");
        Real_AVIStreamGetFrame = (AVIStreamGetFrame_t)GetProcAddress(avifil, "AVIStreamGetFrame");
        Real_AVIStreamGetFrameClose = (AVIStreamGetFrameClose_t)GetProcAddress(avifil, "AVIStreamGetFrameClose");

        Real_AVIStreamRelease = (AVIStreamRelease_t)GetProcAddress(avifil, "AVIStreamRelease");

        // We allow some of these to be null (some games don't import all)
        return true;
    }

    bool InstallHooksInto(HMODULE module)
    {
        bool ok;

        if (!module) return false;

        // FFmpeg one-time: safe to call multiple times
        av_log_set_level(AV_LOG_ERROR);

        if (!ResolveRealAvifil32())
            return false;

		// Patch whatever exists in the IAT and print debug info
        //PatchIAT(module, "avifil32.dll", "AVIFileOpenW", (void*)Hook_AVIFileOpenW, (void**)&Real_AVIFileOpenW);
        ok = PatchIAT(module, "avifil32.dll", "AVIFileOpenW", (void*)Hook_AVIFileOpenW, (void**)&Real_AVIFileOpenW);
        LogPatch("AVIFileOpenW", ok);

        //PatchIAT(module, "avifil32.dll", "AVIFileOpenA", (void*)Hook_AVIFileOpenA, (void**)&Real_AVIFileOpenA);
        ok = PatchIAT(module, "avifil32.dll", "AVIFileOpenA", (void*)Hook_AVIFileOpenA, (void**)&Real_AVIFileOpenA);
        LogPatch("AVIFileOpenA", ok);

        //PatchIAT(module, "avifil32.dll", "AVIFileRelease", (void*)Hook_AVIFileRelease, (void**)&Real_AVIFileRelease);
        ok = PatchIAT(module, "avifil32.dll", "AVIFileRelease", (void*)Hook_AVIFileRelease, (void**)&Real_AVIFileRelease);
        LogPatch("AVIFileRelease", ok);

        //PatchIAT(module, "avifil32.dll", "AVIFileGetStream", (void*)Hook_AVIFileGetStream, (void**)&Real_AVIFileGetStream);
        ok = PatchIAT(module, "avifil32.dll", "AVIFileGetStream", (void*)Hook_AVIFileGetStream, (void**)&Real_AVIFileGetStream);
        LogPatch("AVIFileGetStream", ok);

        //PatchIAT(module, "avifil32.dll", "AVIStreamInfoW", (void*)Hook_AVIStreamInfoW, (void**)&Real_AVIStreamInfoW);
        ok = PatchIAT(module, "avifil32.dll", "AVIStreamInfoW", (void*)Hook_AVIStreamInfoW, (void**)&Real_AVIStreamInfoW);
        LogPatch("AVIStreamInfoW", ok);

        //PatchIAT(module, "avifil32.dll", "AVIStreamInfoA", (void*)Hook_AVIStreamInfoA, (void**)&Real_AVIStreamInfoA);
        ok = PatchIAT(module, "avifil32.dll", "AVIStreamInfoA", (void*)Hook_AVIStreamInfoA, (void**)&Real_AVIStreamInfoA);
        LogPatch("AVIStreamInfoA", ok);

        //PatchIAT(module, "avifil32.dll", "AVIStreamLength", (void*)Hook_AVIStreamLength, (void**)&Real_AVIStreamLength);
        ok = PatchIAT(module, "avifil32.dll", "AVIStreamLength", (void*)Hook_AVIStreamLength, (void**)&Real_AVIStreamLength);
        LogPatch("AVIStreamLength", ok);

        //PatchIAT(module, "avifil32.dll", "AVIStreamSampleToTime", (void*)Hook_AVIStreamSampleToTime, (void**)&Real_AVIStreamSampleToTime);
        ok = PatchIAT(module, "avifil32.dll", "AVIStreamSampleToTime", (void*)Hook_AVIStreamSampleToTime, (void**)&Real_AVIStreamSampleToTime);
        LogPatch("AVIStreamSampleToTime", ok);

        //PatchIAT(module, "avifil32.dll", "AVIStreamGetFrameOpen", (void*)Hook_AVIStreamGetFrameOpen, (void**)&Real_AVIStreamGetFrameOpen);
        ok = PatchIAT(module, "avifil32.dll", "AVIStreamGetFrameOpen", (void*)Hook_AVIStreamGetFrameOpen, (void**)&Real_AVIStreamGetFrameOpen);
        LogPatch("AVIStreamGetFrameOpen", ok);

        //PatchIAT(module, "avifil32.dll", "AVIStreamGetFrame", (void*)Hook_AVIStreamGetFrame, (void**)&Real_AVIStreamGetFrame);
        ok = PatchIAT(module, "avifil32.dll", "AVIStreamGetFrame", (void*)Hook_AVIStreamGetFrame, (void**)&Real_AVIStreamGetFrame);
        LogPatch("AVIStreamGetFrame", ok);

        //PatchIAT(module, "avifil32.dll", "AVIStreamGetFrameClose", (void*)Hook_AVIStreamGetFrameClose, (void**)&Real_AVIStreamGetFrameClose);
        ok = PatchIAT(module, "avifil32.dll", "AVIStreamGetFrameClose", (void*)Hook_AVIStreamGetFrameClose, (void**)&Real_AVIStreamGetFrameClose);
        LogPatch("AVIStreamGetFrameClose", ok);

        //PatchIAT(module, "avifil32.dll", "AVIStreamRelease", (void*)Hook_AVIStreamRelease, (void**)&Real_AVIStreamRelease);
        ok = PatchIAT(module, "avifil32.dll", "AVIStreamRelease", (void*)Hook_AVIStreamRelease, (void**)&Real_AVIStreamRelease);
        LogPatch("AVIStreamRelease", ok);

        return true;
    }
}

namespace AviFfmpegHook
{
    bool InstallForMainModule()
    {
        HMODULE exe = GetModuleHandleW(nullptr);
        return InstallHooksInto(exe);
    }

    bool InstallForModule(HMODULE module)
    {
        return InstallHooksInto(module);
    }
}
