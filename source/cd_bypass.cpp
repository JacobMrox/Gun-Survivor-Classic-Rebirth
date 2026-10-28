#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"
#include "cd_bypass.h"
#include "debug.h"
#include "file_events.h"
//#include "alpha_fix.h"

static void LogMH(const char* what, MH_STATUS s)
{
    char buf[256];
    sprintf_s(buf, "[CD] %s -> %d", what, (int)s);
    //LogFileA(buf);
}

// ===================== Root path helpers =====================
static bool IsRootPathA(LPCSTR p)
{
    // Accept "D:" and "D:\"
    return p && p[0] && p[1] == ':' && (p[2] == 0 || p[2] == '\\' || p[2] == '/');
}

static bool IsRootPathW(LPCWSTR p)
{
    return p && p[0] && p[1] == L':' && (p[2] == 0 || p[2] == L'\\' || p[2] == L'/');
}

// ===================== Originals =====================
static UINT(WINAPI* Real_GetDriveTypeA)(LPCSTR) = nullptr;
static UINT(WINAPI* Real_GetDriveTypeW)(LPCWSTR) = nullptr;

static BOOL(WINAPI* Real_GetVolumeInformationA)(
    LPCSTR, LPSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPSTR, DWORD
    ) = nullptr;

static BOOL(WINAPI* Real_GetVolumeInformationW)(
    LPCWSTR, LPWSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPWSTR, DWORD
    ) = nullptr;

static HANDLE(WINAPI* Real_CreateFileA)(
    LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE
    ) = nullptr;

// ===================== Hooks =====================

static UINT WINAPI Hook_GetDriveTypeA(LPCSTR root)
{
    if (root)
    {
        char buf[256];
        sprintf_s(buf, "[CD] GetDriveTypeA(%s)", root);
        //LogFileA(buf);
    }

    if (IsRootPathA(root))
        return DRIVE_CDROM;

    return Real_GetDriveTypeA ? Real_GetDriveTypeA(root) : DRIVE_FIXED;
}

static UINT WINAPI Hook_GetDriveTypeW(LPCWSTR root)
{
    //LogFileA("[CD] GetDriveTypeW called");

    if (IsRootPathW(root))
        return DRIVE_CDROM;

    return Real_GetDriveTypeW ? Real_GetDriveTypeW(root) : DRIVE_FIXED;
}

static BOOL WINAPI Hook_GetVolumeInformationA(
    LPCSTR root,
    LPSTR volumeName,
    DWORD volumeNameSize,
    LPDWORD volumeSerialNumber,
    LPDWORD maximumComponentLength,
    LPDWORD fileSystemFlags,
    LPSTR fileSystemNameBuffer,
    DWORD fileSystemNameSize)
{
    if (root)
    {
        char buf[256];
        sprintf_s(buf, "[CD] GetVolumeInformationA(%s)", root);
        //LogFileA(buf);
    }

    if (IsRootPathA(root))
    {
        // Your disc label
        if (volumeName && volumeNameSize)
            lstrcpynA(volumeName, "Gun Bio", volumeNameSize);

        // Disc filesystem
        if (fileSystemNameBuffer && fileSystemNameSize)
            lstrcpynA(fileSystemNameBuffer, "CDFS", fileSystemNameSize);

        if (volumeSerialNumber) *volumeSerialNumber = 0x12345678;
        if (maximumComponentLength) *maximumComponentLength = 255;
        if (fileSystemFlags) *fileSystemFlags = 0;

        return TRUE;
    }

    return Real_GetVolumeInformationA
        ? Real_GetVolumeInformationA(root, volumeName, volumeNameSize,
            volumeSerialNumber, maximumComponentLength, fileSystemFlags,
            fileSystemNameBuffer, fileSystemNameSize)
        : FALSE;
}

static BOOL WINAPI Hook_GetVolumeInformationW(
    LPCWSTR root,
    LPWSTR volumeName,
    DWORD volumeNameSize,
    LPDWORD volumeSerialNumber,
    LPDWORD maximumComponentLength,
    LPDWORD fileSystemFlags,
    LPWSTR fileSystemNameBuffer,
    DWORD fileSystemNameSize)
{
    //LogFileA("[CD] GetVolumeInformationW called");

    if (IsRootPathW(root))
    {
        if (volumeName && volumeNameSize)
            lstrcpynW(volumeName, L"Gun Bio", volumeNameSize);

        if (fileSystemNameBuffer && fileSystemNameSize)
            lstrcpynW(fileSystemNameBuffer, L"CDFS", fileSystemNameSize);

        if (volumeSerialNumber) *volumeSerialNumber = 0x12345678;
        if (maximumComponentLength) *maximumComponentLength = 255;
        if (fileSystemFlags) *fileSystemFlags = 0;

        return TRUE;
    }

    return Real_GetVolumeInformationW
        ? Real_GetVolumeInformationW(root, volumeName, volumeNameSize,
            volumeSerialNumber, maximumComponentLength, fileSystemFlags,
            fileSystemNameBuffer, fileSystemNameSize)
        : FALSE;
}

//Hook for CreateFileA to log CD probes and redirect GUN_BIO.EXE access [CD Check]

static bool EndsWithGunBioExeA(const char* p)
{
    // case-insensitive endswith "\GUN_BIO.EXE"
    const char* needle = "\\GUN_BIO.EXE";
    size_t lp = strlen(p);
    size_t ln = strlen(needle);
    if (lp < ln) return false;
    return _stricmp(p + (lp - ln), needle) == 0;
}

static void GetLocalExePathA(char outPath[MAX_PATH])
{
    outPath[0] = 0;
    GetModuleFileNameA(NULL, outPath, MAX_PATH);
}

static HANDLE WINAPI Hook_CreateFileA(
    LPCSTR name,
    DWORD access,
    DWORD share,
    LPSECURITY_ATTRIBUTES sa,
    DWORD creation,
    DWORD flags,
    HANDLE templateFile)
{
    if (name)
    {
		// Report every resource open, not only the special CD executable
		// redirection below. Texture fixes rely on the actual asset identity.
		NotifyFileOpenA(name);
        //Call alpha-fix file open notifier
        //AlphaFix_OnFileOpenA(name);

        // Log only CD-ish probes
        if (!_strnicmp(name, "\\\\.\\", 4) || (name[1] == ':' && (name[2] == 0 || name[2] == '\\' || name[2] == '/')))
        {
            char buf[700];
            sprintf_s(buf, "[CD] CreateFileA(%s)", name);
            //LogFileA(buf);
        }

        // *** The actual bypass ***
        // If the game is probing X:\GUN_BIO.EXE (any drive letter), redirect it
        // to the real local EXE path (the running gun_bio.exe).
        if (name[1] == ':' && name[2] == '\\' && EndsWithGunBioExeA(name))
        {
            char localExe[MAX_PATH];
            GetLocalExePathA(localExe);

            char buf[700];
            sprintf_s(buf, "[CD] Redirect %s -> %s", name, localExe);
            //LogFileA(buf);

            return Real_CreateFileA
                ? Real_CreateFileA(localExe, access, share, sa, creation, flags, templateFile)
                : INVALID_HANDLE_VALUE;
        }
    }

    return Real_CreateFileA
        ? Real_CreateFileA(name, access, share, sa, creation, flags, templateFile)
        : INVALID_HANDLE_VALUE;
}

// ===================== Installer =====================

static void HookOne(HMODULE mod, const char* procName, LPVOID detour, LPVOID* original)
{
    LPVOID p = (LPVOID)GetProcAddress(mod, procName);
    if (!p)
    {
        char buf[256];
        sprintf_s(buf, "GetProcAddress failed: %s", procName);
        //LogFileA(buf);
        return;
    }

    MH_STATUS s = MH_CreateHook(p, detour, original);
    if (s != MH_OK)
    {
        char buf[256];
        sprintf_s(buf, "MH_CreateHook %s", procName);
        LogMH(buf, s);
        return;
    }

    s = MH_EnableHook(p);
    if (s != MH_OK)
    {
        char buf[256];
        sprintf_s(buf, "MH_EnableHook %s", procName);
        LogMH(buf, s);
        return;
    }

    char ok[256];
    sprintf_s(ok, "[CD] Hooked %s", procName);
    //LogFileA(ok);
}

extern "C" __declspec(dllexport) void InstallCDBypass()
{
    static bool once = false;
    if (once) return;
    once = true;

    //LogFileA("========== InstallCDBypass ==========");

    MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
    {
        LogMH("MH_Initialize", init);
        return;
    }

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32)
    {
        //LogFileA("[CD] kernel32.dll not found??");
        return;
    }

    // Drive/volume spoof
    HookOne(k32, "GetDriveTypeA", (LPVOID)&Hook_GetDriveTypeA, (LPVOID*)&Real_GetDriveTypeA);
    HookOne(k32, "GetDriveTypeW", (LPVOID)&Hook_GetDriveTypeW, (LPVOID*)&Real_GetDriveTypeW);

    HookOne(k32, "GetVolumeInformationA", (LPVOID)&Hook_GetVolumeInformationA, (LPVOID*)&Real_GetVolumeInformationA);
    HookOne(k32, "GetVolumeInformationW", (LPVOID)&Hook_GetVolumeInformationW, (LPVOID*)&Real_GetVolumeInformationW);

    // Logging-only file probe visibility (should not hang)
    HookOne(k32, "CreateFileA", (LPVOID)&Hook_CreateFileA, (LPVOID*)&Real_CreateFileA);

    LogLine("[CD] InstallCDBypass complete");
}
