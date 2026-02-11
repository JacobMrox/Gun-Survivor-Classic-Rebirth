#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <imagehlp.h>
#include <psapi.h>
#include <vfw.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "imagehlp.lib")
#pragma comment(lib, "psapi.lib")

#include "AviFfmpegHook.h"
#include "debug.h"

namespace
{
    // ------------------------------------------------------------
    // Game dir filter (prevents patching system DLLs)
    // ------------------------------------------------------------
    static char gGameDir[MAX_PATH] = {};

    static void InitGameDirOnce()
    {
        if (gGameDir[0]) return;
        GetModuleFileNameA(nullptr, gGameDir, MAX_PATH);
        char* slash = strrchr(gGameDir, '\\');
        if (slash) *(slash + 1) = 0; // keep trailing backslash
    }

    static bool IsGameDirModule(HMODULE m)
    {
        if (!m) return false;

        InitGameDirOnce();

        char modPath[MAX_PATH] = {};
        if (!GetModuleFileNameA(m, modPath, MAX_PATH))
            return false;

        // case-insensitive prefix match
        return _strnicmp(modPath, gGameDir, strlen(gGameDir)) == 0;
    }

    // ------------------------------------------------------------
    // IAT patch helper
    // ------------------------------------------------------------
    bool PatchIAT(HMODULE module, const char* importedDll, const char* funcName, void* hookFn, void** realFnOut)
    {
        if (!module) return false;

        ULONG size = 0;
        auto* impDesc = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToData(
            module, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &size);

        if (!impDesc) return false;

        for (; impDesc->Name; impDesc++)
        {
            const char* dllName = (const char*)((BYTE*)module + impDesc->Name);
            if (!dllName) continue;
            if (_stricmp(dllName, importedDll) != 0) continue;

            auto* thunk = (PIMAGE_THUNK_DATA)((BYTE*)module + impDesc->FirstThunk);
            auto* orig = (PIMAGE_THUNK_DATA)((BYTE*)module + impDesc->OriginalFirstThunk);
            if (!thunk || !orig) return false;

            for (; orig->u1.AddressOfData; thunk++, orig++)
            {
                if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;

                auto* ibn = (PIMAGE_IMPORT_BY_NAME)((BYTE*)module + orig->u1.AddressOfData);
                if (!ibn || !ibn->Name) continue;

                if (strcmp((const char*)ibn->Name, funcName) != 0) continue;

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

    static void LogPatch(const char* what, bool ok)
    {
        // Uncomment when needed:
        // LogLine("[AviFfmpegHook] IAT patch %s: %s", what, ok ? "OK" : "NOT FOUND");
        (void)what; (void)ok;
    }

    // ------------------------------------------------------------
    // Real AVIFIL32 (pass-through for now)
    // ------------------------------------------------------------
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

    static HMODULE gAvifil = nullptr;

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

    static bool ResolveRealAvifil32()
    {
        if (gAvifil) return true;

        // Load only once, from our init thread (not from inside loader hooks)
        gAvifil = LoadLibraryW(L"avifil32.dll");
        if (!gAvifil) return false;

        Real_AVIFileOpenW = (AVIFileOpenW_t)GetProcAddress(gAvifil, "AVIFileOpenW");
        Real_AVIFileOpenA = (AVIFileOpenA_t)GetProcAddress(gAvifil, "AVIFileOpenA");
        Real_AVIFileRelease = (AVIFileRelease_t)GetProcAddress(gAvifil, "AVIFileRelease");
        Real_AVIFileGetStream = (AVIFileGetStream_t)GetProcAddress(gAvifil, "AVIFileGetStream");

        Real_AVIStreamInfoW = (AVIStreamInfoW_t)GetProcAddress(gAvifil, "AVIStreamInfoW");
        Real_AVIStreamInfoA = (AVIStreamInfoA_t)GetProcAddress(gAvifil, "AVIStreamInfoA");
        Real_AVIStreamLength = (AVIStreamLength_t)GetProcAddress(gAvifil, "AVIStreamLength");
        Real_AVIStreamSampleToTime = (AVIStreamSampleToTime_t)GetProcAddress(gAvifil, "AVIStreamSampleToTime");

        Real_AVIStreamGetFrameOpen = (AVIStreamGetFrameOpen_t)GetProcAddress(gAvifil, "AVIStreamGetFrameOpen");
        Real_AVIStreamGetFrame = (AVIStreamGetFrame_t)GetProcAddress(gAvifil, "AVIStreamGetFrame");
        Real_AVIStreamGetFrameClose = (AVIStreamGetFrameClose_t)GetProcAddress(gAvifil, "AVIStreamGetFrameClose");

        Real_AVIStreamRelease = (AVIStreamRelease_t)GetProcAddress(gAvifil, "AVIStreamRelease");

        return true;
    }

    // ------------------------------------------------------------
    // Hooked AVIFIL32 exports (pass-through + minimal log)
    // ------------------------------------------------------------
    static HRESULT WINAPI Hook_AVIFileOpenW(PAVIFILE* ppfile, LPCWSTR szFile, UINT uMode, LPCLSID lpHandler)
    {
        LogLine("[AVI] AVIFileOpenW: %S", szFile ? szFile : L"(null)");
        return Real_AVIFileOpenW ? Real_AVIFileOpenW(ppfile, szFile, uMode, lpHandler) : AVIERR_ERROR;
    }

    static HRESULT WINAPI Hook_AVIFileOpenA(PAVIFILE* ppfile, LPCSTR szFile, UINT uMode, LPCLSID lpHandler)
    {
        LogLine("[AVI] AVIFileOpenA: %s", szFile ? szFile : "(null)");
        return Real_AVIFileOpenA ? Real_AVIFileOpenA(ppfile, szFile, uMode, lpHandler) : AVIERR_ERROR;
    }

    static ULONG WINAPI Hook_AVIFileRelease(PAVIFILE pfile)
    {
        LogLine("[AVI] AVIFileRelease");
        return Real_AVIFileRelease ? Real_AVIFileRelease(pfile) : 0;
    }

    static HRESULT WINAPI Hook_AVIFileGetStream(PAVIFILE pfile, PAVISTREAM* ppStream, DWORD fccType, LONG lParam)
    {
        LogLine("[AVI] AVIFileGetStream fccType=0x%08X", (unsigned)fccType);
        return Real_AVIFileGetStream ? Real_AVIFileGetStream(pfile, ppStream, fccType, lParam) : AVIERR_NODATA;
    }

    static HRESULT WINAPI Hook_AVIStreamInfoW(PAVISTREAM pStream, AVISTREAMINFOW* psi, LONG lSize)
    {
        return Real_AVIStreamInfoW ? Real_AVIStreamInfoW(pStream, psi, lSize) : AVIERR_ERROR;
    }

    static HRESULT WINAPI Hook_AVIStreamInfoA(PAVISTREAM pStream, AVISTREAMINFOA* psi, LONG lSize)
    {
        return Real_AVIStreamInfoA ? Real_AVIStreamInfoA(pStream, psi, lSize) : AVIERR_ERROR;
    }

    static LONG WINAPI Hook_AVIStreamLength(PAVISTREAM pStream)
    {
        return Real_AVIStreamLength ? Real_AVIStreamLength(pStream) : 0;
    }

    static LONG WINAPI Hook_AVIStreamSampleToTime(PAVISTREAM pStream, LONG lSample)
    {
        return Real_AVIStreamSampleToTime ? Real_AVIStreamSampleToTime(pStream, lSample) : 0;
    }

    static PGETFRAME WINAPI Hook_AVIStreamGetFrameOpen(PAVISTREAM pStream, LPBITMAPINFOHEADER lpbiWanted)
    {
        LogLine("[AVI] AVIStreamGetFrameOpen");
        return Real_AVIStreamGetFrameOpen ? Real_AVIStreamGetFrameOpen(pStream, lpbiWanted) : nullptr;
    }

    static LPVOID WINAPI Hook_AVIStreamGetFrame(PGETFRAME pg, LONG lPos)
    {
        return Real_AVIStreamGetFrame ? Real_AVIStreamGetFrame(pg, lPos) : nullptr;
    }

    static HRESULT WINAPI Hook_AVIStreamGetFrameClose(PGETFRAME pg)
    {
        LogLine("[AVI] AVIStreamGetFrameClose");
        return Real_AVIStreamGetFrameClose ? Real_AVIStreamGetFrameClose(pg) : AVIERR_ERROR;
    }

    static ULONG WINAPI Hook_AVIStreamRelease(PAVISTREAM pStream)
    {
        LogLine("[AVI] AVIStreamRelease");
        return Real_AVIStreamRelease ? Real_AVIStreamRelease(pStream) : 0;
    }

    // ------------------------------------------------------------
    // Patch AVIFIL32 imports in a single module
    // ------------------------------------------------------------
    static void PatchAvifilImportsInModule(HMODULE m)
    {
        if (!m) return;

        bool ok;
        ok = PatchIAT(m, "avifil32.dll", "AVIFileOpenW", (void*)Hook_AVIFileOpenW, (void**)&Real_AVIFileOpenW); LogPatch("AVIFileOpenW", ok);
        ok = PatchIAT(m, "avifil32.dll", "AVIFileOpenA", (void*)Hook_AVIFileOpenA, (void**)&Real_AVIFileOpenA); LogPatch("AVIFileOpenA", ok);
        ok = PatchIAT(m, "avifil32.dll", "AVIFileRelease", (void*)Hook_AVIFileRelease, (void**)&Real_AVIFileRelease); LogPatch("AVIFileRelease", ok);
        ok = PatchIAT(m, "avifil32.dll", "AVIFileGetStream", (void*)Hook_AVIFileGetStream, (void**)&Real_AVIFileGetStream); LogPatch("AVIFileGetStream", ok);

        ok = PatchIAT(m, "avifil32.dll", "AVIStreamInfoW", (void*)Hook_AVIStreamInfoW, (void**)&Real_AVIStreamInfoW); LogPatch("AVIStreamInfoW", ok);
        ok = PatchIAT(m, "avifil32.dll", "AVIStreamInfoA", (void*)Hook_AVIStreamInfoA, (void**)&Real_AVIStreamInfoA); LogPatch("AVIStreamInfoA", ok);
        ok = PatchIAT(m, "avifil32.dll", "AVIStreamLength", (void*)Hook_AVIStreamLength, (void**)&Real_AVIStreamLength); LogPatch("AVIStreamLength", ok);
        ok = PatchIAT(m, "avifil32.dll", "AVIStreamSampleToTime", (void*)Hook_AVIStreamSampleToTime, (void**)&Real_AVIStreamSampleToTime); LogPatch("AVIStreamSampleToTime", ok);

        ok = PatchIAT(m, "avifil32.dll", "AVIStreamGetFrameOpen", (void*)Hook_AVIStreamGetFrameOpen, (void**)&Real_AVIStreamGetFrameOpen); LogPatch("AVIStreamGetFrameOpen", ok);
        ok = PatchIAT(m, "avifil32.dll", "AVIStreamGetFrame", (void*)Hook_AVIStreamGetFrame, (void**)&Real_AVIStreamGetFrame); LogPatch("AVIStreamGetFrame", ok);
        ok = PatchIAT(m, "avifil32.dll", "AVIStreamGetFrameClose", (void*)Hook_AVIStreamGetFrameClose, (void**)&Real_AVIStreamGetFrameClose); LogPatch("AVIStreamGetFrameClose", ok);

        ok = PatchIAT(m, "avifil32.dll", "AVIStreamRelease", (void*)Hook_AVIStreamRelease, (void**)&Real_AVIStreamRelease); LogPatch("AVIStreamRelease", ok);
    }

    // ------------------------------------------------------------
    // Kernel32 hooks (only patch game-folder modules)
    // ------------------------------------------------------------
    using LoadLibraryA_t = HMODULE(WINAPI*)(LPCSTR);
    using LoadLibraryW_t = HMODULE(WINAPI*)(LPCWSTR);
    using LoadLibraryExA_t = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
    using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
    using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);

    static LoadLibraryA_t   Real_LoadLibraryA = nullptr;
    static LoadLibraryW_t   Real_LoadLibraryW = nullptr;
    static LoadLibraryExA_t Real_LoadLibraryExA = nullptr;
    static LoadLibraryExW_t Real_LoadLibraryExW = nullptr;
    static GetProcAddress_t Real_GetProcAddress = nullptr;

    static thread_local bool gInLoaderHook = false;

    static bool IsAvifilHandle(HMODULE mod)
    {
        if (!mod) return false;
        if (gAvifil && mod == gAvifil) return true;
        HMODULE h = GetModuleHandleW(L"avifil32.dll");
        return (h && mod == h);
    }

    static void PatchKernelImportsInModule(HMODULE m);

    static void PatchIfGameModule(HMODULE mod)
    {
        if (!mod) return;

        // Always allow EXE, and allow game-folder modules.
        HMODULE exe = GetModuleHandleW(nullptr);
        if (mod != exe && !IsGameDirModule(mod))
            return;

        PatchAvifilImportsInModule(mod);
        PatchKernelImportsInModule(mod);
    }

    static HMODULE WINAPI Hook_LoadLibraryA(LPCSTR s)
    {
        if (gInLoaderHook) return Real_LoadLibraryA ? Real_LoadLibraryA(s) : nullptr;
        gInLoaderHook = true;

        HMODULE mod = Real_LoadLibraryA ? Real_LoadLibraryA(s) : nullptr;
        PatchIfGameModule(mod);

        gInLoaderHook = false;
        return mod;
    }

    static HMODULE WINAPI Hook_LoadLibraryW(LPCWSTR s)
    {
        if (gInLoaderHook) return Real_LoadLibraryW ? Real_LoadLibraryW(s) : nullptr;
        gInLoaderHook = true;

        HMODULE mod = Real_LoadLibraryW ? Real_LoadLibraryW(s) : nullptr;
        PatchIfGameModule(mod);

        gInLoaderHook = false;
        return mod;
    }

    static HMODULE WINAPI Hook_LoadLibraryExA(LPCSTR s, HANDLE f, DWORD fl)
    {
        if (gInLoaderHook) return Real_LoadLibraryExA ? Real_LoadLibraryExA(s, f, fl) : nullptr;
        gInLoaderHook = true;

        HMODULE mod = Real_LoadLibraryExA ? Real_LoadLibraryExA(s, f, fl) : nullptr;
        PatchIfGameModule(mod);

        gInLoaderHook = false;
        return mod;
    }

    static HMODULE WINAPI Hook_LoadLibraryExW(LPCWSTR s, HANDLE f, DWORD fl)
    {
        if (gInLoaderHook) return Real_LoadLibraryExW ? Real_LoadLibraryExW(s, f, fl) : nullptr;
        gInLoaderHook = true;

        HMODULE mod = Real_LoadLibraryExW ? Real_LoadLibraryExW(s, f, fl) : nullptr;
        PatchIfGameModule(mod);

        gInLoaderHook = false;
        return mod;
    }

    static FARPROC WINAPI Hook_GetProcAddress(HMODULE mod, LPCSTR name)
    {
        if (gInLoaderHook)
            return Real_GetProcAddress ? Real_GetProcAddress(mod, name) : nullptr;

        if (mod && name && IsAvifilHandle(mod))
        {
            if (strcmp(name, "AVIFileOpenW") == 0) return (FARPROC)Hook_AVIFileOpenW;
            if (strcmp(name, "AVIFileOpenA") == 0) return (FARPROC)Hook_AVIFileOpenA;
            if (strcmp(name, "AVIFileRelease") == 0) return (FARPROC)Hook_AVIFileRelease;
            if (strcmp(name, "AVIFileGetStream") == 0) return (FARPROC)Hook_AVIFileGetStream;

            if (strcmp(name, "AVIStreamInfoW") == 0) return (FARPROC)Hook_AVIStreamInfoW;
            if (strcmp(name, "AVIStreamInfoA") == 0) return (FARPROC)Hook_AVIStreamInfoA;
            if (strcmp(name, "AVIStreamLength") == 0) return (FARPROC)Hook_AVIStreamLength;
            if (strcmp(name, "AVIStreamSampleToTime") == 0) return (FARPROC)Hook_AVIStreamSampleToTime;

            if (strcmp(name, "AVIStreamGetFrameOpen") == 0) return (FARPROC)Hook_AVIStreamGetFrameOpen;
            if (strcmp(name, "AVIStreamGetFrame") == 0) return (FARPROC)Hook_AVIStreamGetFrame;
            if (strcmp(name, "AVIStreamGetFrameClose") == 0) return (FARPROC)Hook_AVIStreamGetFrameClose;

            if (strcmp(name, "AVIStreamRelease") == 0) return (FARPROC)Hook_AVIStreamRelease;
        }

        return Real_GetProcAddress ? Real_GetProcAddress(mod, name) : nullptr;
    }

    static void PatchKernelImportsInModule(HMODULE m)
    {
        if (!m) return;

        PatchIAT(m, "kernel32.dll", "LoadLibraryA", (void*)Hook_LoadLibraryA, (void**)&Real_LoadLibraryA);
        PatchIAT(m, "kernel32.dll", "LoadLibraryW", (void*)Hook_LoadLibraryW, (void**)&Real_LoadLibraryW);
        PatchIAT(m, "kernel32.dll", "LoadLibraryExA", (void*)Hook_LoadLibraryExA, (void**)&Real_LoadLibraryExA);
        PatchIAT(m, "kernel32.dll", "LoadLibraryExW", (void*)Hook_LoadLibraryExW, (void**)&Real_LoadLibraryExW);
        PatchIAT(m, "kernel32.dll", "GetProcAddress", (void*)Hook_GetProcAddress, (void**)&Real_GetProcAddress);

        PatchIAT(m, "kernelbase.dll", "LoadLibraryA", (void*)Hook_LoadLibraryA, (void**)&Real_LoadLibraryA);
        PatchIAT(m, "kernelbase.dll", "LoadLibraryW", (void*)Hook_LoadLibraryW, (void**)&Real_LoadLibraryW);
        PatchIAT(m, "kernelbase.dll", "LoadLibraryExA", (void*)Hook_LoadLibraryExA, (void**)&Real_LoadLibraryExA);
        PatchIAT(m, "kernelbase.dll", "LoadLibraryExW", (void*)Hook_LoadLibraryExW, (void**)&Real_LoadLibraryExW);
        PatchIAT(m, "kernelbase.dll", "GetProcAddress", (void*)Hook_GetProcAddress, (void**)&Real_GetProcAddress);
    }

    static void PatchAllLoadedModulesOnce()
    {
        HMODULE mods[1024]{};
        DWORD needed = 0;
        if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed))
            return;

        HMODULE exe = GetModuleHandleW(nullptr);

        DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; i++)
        {
            HMODULE m = mods[i];
            if (!m) continue;

            // ✅ only patch EXE + game-folder DLLs
            if (m != exe && !IsGameDirModule(m))
                continue;

            PatchAvifilImportsInModule(m);
            PatchKernelImportsInModule(m);
        }
    }
}

namespace AviFfmpegHook
{
    bool Install()
    {
        static LONG sOnce = 0;
        if (InterlockedExchange(&sOnce, 1) != 0)
            return true;

        LogLine("[AviFfmpegHook] Install ENTER");

        InitGameDirOnce();

        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        HMODULE kb = GetModuleHandleW(L"kernelbase.dll");

        Real_LoadLibraryA = (LoadLibraryA_t)GetProcAddress(k32 ? k32 : kb, "LoadLibraryA");
        Real_LoadLibraryW = (LoadLibraryW_t)GetProcAddress(k32 ? k32 : kb, "LoadLibraryW");
        Real_LoadLibraryExA = (LoadLibraryExA_t)GetProcAddress(k32 ? k32 : kb, "LoadLibraryExA");
        Real_LoadLibraryExW = (LoadLibraryExW_t)GetProcAddress(k32 ? k32 : kb, "LoadLibraryExW");
        Real_GetProcAddress = (GetProcAddress_t)GetProcAddress(k32 ? k32 : kb, "GetProcAddress");

        if (!ResolveRealAvifil32())
            LogLine("[AviFfmpegHook] Note: avifil32.dll could not be loaded now (still catches via GetProcAddress).");

        PatchAllLoadedModulesOnce();

        LogLine("[AviFfmpegHook] Install EXIT");
        return true;
    }
}
