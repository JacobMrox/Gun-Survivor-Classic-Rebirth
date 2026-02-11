#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <imagehlp.h>
#include <psapi.h>
#include <mmsystem.h> // MCI, mciSendString, mciSendCommand
#include <cstdio>
#include <cstring>

#pragma comment(lib, "imagehlp.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winmm.lib")

#include "MciHook.h"
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

        return _strnicmp(modPath, gGameDir, strlen(gGameDir)) == 0;
    }

    static bool IsGameModule(HMODULE m)
    {
        if (!m) return false;
        HMODULE exe = GetModuleHandleW(nullptr);
        return (m == exe) || IsGameDirModule(m);
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

    // ------------------------------------------------------------
    // Real WINMM MCI functions
    // ------------------------------------------------------------
    using mciSendStringA_t = MCIERROR(WINAPI*)(LPCSTR, LPSTR, UINT, HANDLE);
    using mciSendStringW_t = MCIERROR(WINAPI*)(LPCWSTR, LPWSTR, UINT, HANDLE);
    using mciSendCommandA_t = MCIERROR(WINAPI*)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);
    using mciSendCommandW_t = MCIERROR(WINAPI*)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);

    static HMODULE gWinmm = nullptr;

    static mciSendStringA_t  Real_mciSendStringA = nullptr;
    static mciSendStringW_t  Real_mciSendStringW = nullptr;
    static mciSendCommandA_t Real_mciSendCommandA = nullptr;
    static mciSendCommandW_t Real_mciSendCommandW = nullptr;

    static bool ResolveRealWinmm()
    {
        if (gWinmm) return true;

        // Safe load (from init thread).
        gWinmm = LoadLibraryW(L"winmm.dll");
        if (!gWinmm) return false;

        Real_mciSendStringA = (mciSendStringA_t)GetProcAddress(gWinmm, "mciSendStringA");
        Real_mciSendStringW = (mciSendStringW_t)GetProcAddress(gWinmm, "mciSendStringW");
        Real_mciSendCommandA = (mciSendCommandA_t)GetProcAddress(gWinmm, "mciSendCommandA");
        Real_mciSendCommandW = (mciSendCommandW_t)GetProcAddress(gWinmm, "mciSendCommandW");

        return true;
    }

    // ------------------------------------------------------------
    // Logging helpers
    // ------------------------------------------------------------
    static void LogMciStringA(const char* cmd)
    {
        if (!cmd) { LogLine("[MCI] mciSendStringA: (null)"); return; }
        // Avoid crazy-long lines
        char buf[512]{};
        strncpy_s(buf, cmd, sizeof(buf) - 1);
        LogLine("[MCI] mciSendStringA: %s", buf);
    }

    static void LogMciStringW(const wchar_t* cmd)
    {
        if (!cmd) { LogLine("[MCI] mciSendStringW: (null)"); return; }
        wchar_t buf[512]{};
        wcsncpy_s(buf, cmd, _TRUNCATE);
        LogLine("[MCI] mciSendStringW: %S", buf);
    }

    static const char* MciCmdName(UINT msg)
    {
        switch (msg)
        {
        case MCI_OPEN:  return "MCI_OPEN";
        case MCI_CLOSE: return "MCI_CLOSE";
        case MCI_PLAY:  return "MCI_PLAY";
        case MCI_STOP:  return "MCI_STOP";
        case MCI_SEEK:  return "MCI_SEEK";
        case MCI_PAUSE: return "MCI_PAUSE";
        case MCI_RESUME:return "MCI_RESUME";
        default:        return "MCI_???";
        }
    }

    // ------------------------------------------------------------
    // Hooked WINMM exports (pass-through + log)
    // ------------------------------------------------------------
    static MCIERROR WINAPI Hook_mciSendStringA(LPCSTR cmd, LPSTR ret, UINT cchRet, HANDLE cb)
    {
        LogMciStringA(cmd);
        return Real_mciSendStringA ? Real_mciSendStringA(cmd, ret, cchRet, cb) : (MCIERROR)MCIERR_DRIVER;
    }

    static MCIERROR WINAPI Hook_mciSendStringW(LPCWSTR cmd, LPWSTR ret, UINT cchRet, HANDLE cb)
    {
        LogMciStringW(cmd);
        return Real_mciSendStringW ? Real_mciSendStringW(cmd, ret, cchRet, cb) : (MCIERROR)MCIERR_DRIVER;
    }

    static MCIERROR WINAPI Hook_mciSendCommandA(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR params)
    {
        LogLine("[MCI] mciSendCommandA: id=%u msg=%s(0x%X) flags=0x%p params=0x%p",
            (unsigned)id, MciCmdName(msg), (unsigned)msg, (void*)flags, (void*)params);

        return Real_mciSendCommandA ? Real_mciSendCommandA(id, msg, flags, params) : (MCIERROR)MCIERR_DRIVER;
    }

    static MCIERROR WINAPI Hook_mciSendCommandW(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR params)
    {
        LogLine("[MCI] mciSendCommandW: id=%u msg=%s(0x%X) flags=0x%p params=0x%p",
            (unsigned)id, MciCmdName(msg), (unsigned)msg, (void*)flags, (void*)params);

        return Real_mciSendCommandW ? Real_mciSendCommandW(id, msg, flags, params) : (MCIERROR)MCIERR_DRIVER;
    }

    // ------------------------------------------------------------
    // Patch WINMM imports in a module
    // ------------------------------------------------------------
    static void PatchWinmmImportsInModule(HMODULE m)
    {
        if (!m) return;

        PatchIAT(m, "winmm.dll", "mciSendStringA", (void*)Hook_mciSendStringA, (void**)&Real_mciSendStringA);
        PatchIAT(m, "winmm.dll", "mciSendStringW", (void*)Hook_mciSendStringW, (void**)&Real_mciSendStringW);
        PatchIAT(m, "winmm.dll", "mciSendCommandA", (void*)Hook_mciSendCommandA, (void**)&Real_mciSendCommandA);
        PatchIAT(m, "winmm.dll", "mciSendCommandW", (void*)Hook_mciSendCommandW, (void**)&Real_mciSendCommandW);
    }

    // ------------------------------------------------------------
    // Kernel32 hooks (for late-loaded modules / dynamic resolves)
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

    static bool IsWinmmHandle(HMODULE mod)
    {
        if (!mod) return false;
        if (gWinmm && mod == gWinmm) return true;
        HMODULE h = GetModuleHandleW(L"winmm.dll");
        return (h && mod == h);
    }

    static void PatchKernelImportsInModule(HMODULE m);

    static void PatchIfGameModule(HMODULE mod)
    {
        if (!mod) return;
        if (!IsGameModule(mod)) return;

        PatchWinmmImportsInModule(mod);
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

        // Redirect dynamic resolves for WINMM MCI
        if (mod && name && IsWinmmHandle(mod))
        {
            if (strcmp(name, "mciSendStringA") == 0)  return (FARPROC)Hook_mciSendStringA;
            if (strcmp(name, "mciSendStringW") == 0)  return (FARPROC)Hook_mciSendStringW;
            if (strcmp(name, "mciSendCommandA") == 0) return (FARPROC)Hook_mciSendCommandA;
            if (strcmp(name, "mciSendCommandW") == 0) return (FARPROC)Hook_mciSendCommandW;
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

        DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; i++)
        {
            HMODULE m = mods[i];
            if (!m) continue;
            if (!IsGameModule(m)) continue;

            PatchWinmmImportsInModule(m);
            PatchKernelImportsInModule(m);
        }
    }
}

namespace MciHook
{
    bool Install()
    {
        static LONG sOnce = 0;
        if (InterlockedExchange(&sOnce, 1) != 0)
            return true;

        LogLine("[MciHook] Install ENTER");
        InitGameDirOnce();

        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        HMODULE kb = GetModuleHandleW(L"kernelbase.dll");

        Real_LoadLibraryA = (LoadLibraryA_t)GetProcAddress(k32 ? k32 : kb, "LoadLibraryA");
        Real_LoadLibraryW = (LoadLibraryW_t)GetProcAddress(k32 ? k32 : kb, "LoadLibraryW");
        Real_LoadLibraryExA = (LoadLibraryExA_t)GetProcAddress(k32 ? k32 : kb, "LoadLibraryExA");
        Real_LoadLibraryExW = (LoadLibraryExW_t)GetProcAddress(k32 ? k32 : kb, "LoadLibraryExW");
        Real_GetProcAddress = (GetProcAddress_t)GetProcAddress(k32 ? k32 : kb, "GetProcAddress");

        if (!ResolveRealWinmm())
            LogLine("[MciHook] Note: winmm.dll could not be loaded now (still catches via GetProcAddress).");

        PatchAllLoadedModulesOnce();

        LogLine("[MciHook] Install EXIT");
        return true;
    }
}
