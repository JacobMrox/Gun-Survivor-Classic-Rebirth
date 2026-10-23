// dinputhook.cpp
#pragma comment(lib, "dxguid.lib")
#include <windows.h>
#include <dinput.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "MinHook.h"
#include "aim_shared.h"
#include "debug.h"
#include "survivor_pc.h"

// ---- shared globals we already use in EndScene ----
extern HWND  g_hFocusWindow;
extern float gAimX;
extern float gAimY;
extern bool gAimInit;

// Optional tuning
// MouseSensitivity and EqualMouseAxes ([SurvivorFix] in config.ini, survivor_pc.cpp).
#define gMouseMulX SurvivorPc_MouseMulX()
#define gMouseMulY SurvivorPc_MouseMulY()
static bool  gInvertY = false;
bool gUseRelativeDIForCrosshair = false;

static inline bool IsOurWindowFocused()
{
    if (!g_hFocusWindow) return false;
    return GetForegroundWindow() == g_hFocusWindow;
}

// -------------------- DirectInputCreateA hook --------------------
using DirectInputCreateA_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
static DirectInputCreateA_t Real_DirectInputCreateA = nullptr;

// -------------------- vtable indices --------------------
// IDirectInputA::CreateDevice is method #3 in the COM interface (after IUnknown 0..2)
static constexpr int VTI_DI_CreateDevice = 3;

// IDirectInputDeviceA methods start after IUnknown (0..2):
// GetDeviceData is later; safest is to hook by grabbing the vtable pointer and scanning known slots is overkill.
// We¡¦ll use the standard DI7 layout index for GetDeviceData (works in most builds).
static constexpr int VTI_DID_GetDeviceData = 10; // Common DI7A layout (Acquire=7, Unacquire=8, GetDeviceState=9, GetDeviceData=10)

// -------------------- CreateDevice detour --------------------
using DI_CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(LPDIRECTINPUTA, REFGUID, LPDIRECTINPUTDEVICEA*, LPUNKNOWN);
static DI_CreateDevice_t Real_DI_CreateDevice = nullptr;

// -------------------- GetDeviceData detour --------------------
using DID_GetDeviceData_t = HRESULT(STDMETHODCALLTYPE*)(LPDIRECTINPUTDEVICEA, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
static DID_GetDeviceData_t Real_GetDeviceData = nullptr;

// -------------------- SetProperty detour (MouseBufferSize, survivor_pc.cpp) --------------------
// The game asks for a 16-event mouse buffer (gun_bio 004069F1) and reads 16 events per frame: a modern
// mouse sends far more, the rest is lost and the speed depends on the frame rate. Survivor Fix makes the
// game read the whole buffer every frame; here the buffer itself gets bigger.
static constexpr int VTI_DID_SetProperty = 6;
using DID_SetProperty_t = HRESULT(STDMETHODCALLTYPE*)(LPDIRECTINPUTDEVICEA, REFGUID, LPCDIPROPHEADER);
static DID_SetProperty_t Real_SetProperty = nullptr;
static LPDIRECTINPUTDEVICEA gMouseDevice = nullptr;

static HRESULT STDMETHODCALLTYPE Hook_SetProperty(LPDIRECTINPUTDEVICEA This, REFGUID rguidProp, LPCDIPROPHEADER pdiph)
{
    const DWORD wanted = SurvivorPc_MouseBufferSize();
    if (This == gMouseDevice && wanted && reinterpret_cast<uintptr_t>(&rguidProp) == 1 /* DIPROP_BUFFERSIZE */ && pdiph &&
        pdiph->dwSize == sizeof(DIPROPDWORD))
    {
        DIPROPDWORD prop = *reinterpret_cast<const DIPROPDWORD*>(pdiph);
        if (prop.dwData < wanted)
        {
            LogLine("[DI] Mouse buffer %lu -> %lu events", prop.dwData, wanted);
            prop.dwData = wanted;
            return Real_SetProperty(This, rguidProp, &prop.diph);
        }
    }
    return Real_SetProperty(This, rguidProp, pdiph);
}

#pragma pack(push, 1)
struct DIOBJECTDATA_SMALL
{
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
}; // 16 bytes (common)
#pragma pack(pop)


// DI7-sized object (16 bytes)
#pragma pack(push, 1)
struct DIDEVICEOBJECTDATA16 {
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
};
#pragma pack(pop)

static HRESULT STDMETHODCALLTYPE Hook_GetDeviceData(
    LPDIRECTINPUTDEVICEA This,
    DWORD cbObjectData,
    LPDIDEVICEOBJECTDATA rgdod,
    LPDWORD pdwInOut,
    DWORD dwFlags)
{
    HRESULT hr = Real_GetDeviceData(This, cbObjectData, rgdod, pdwInOut, dwFlags);
    if (FAILED(hr) || !rgdod || !pdwInOut || *pdwInOut == 0)
        return hr;

    if (!IsOurWindowFocused())
        return hr;

    if (cbObjectData == sizeof(DIDEVICEOBJECTDATA))
    {
        // parse as DIDEVICEOBJECTDATA (same as above)
    }
    else if (cbObjectData == sizeof(DIOBJECTDATA_SMALL))
    {
        auto* ev = (const DIOBJECTDATA_SMALL*)rgdod;
        for (DWORD i = 0; i < *pdwInOut; i++)
        {
            if (ev[i].dwOfs == DIMOFS_X) gAimX += (LONG)ev[i].dwData * gMouseMulX;
            else if (ev[i].dwOfs == DIMOFS_Y) gAimY += (LONG)ev[i].dwData * gMouseMulY;
        }
    }
    else
    {
        return hr;
    }

    // We only know how to parse the standard DIDEVICEOBJECTDATA layout.
    // If the game asks for a smaller struct, DO NOT TOUCH IT (prevents crazy dx/dy).
    if (cbObjectData != sizeof(DIDEVICEOBJECTDATA))
        return hr;

    for (DWORD i = 0; i < *pdwInOut; i++)
    {
        const DIDEVICEOBJECTDATA& e = rgdod[i];

        if (e.dwOfs == DIMOFS_X)
        {
            LONG dx = (LONG)e.dwData;
            gAimX += (float)dx * gMouseMulX;
        }
        else if (e.dwOfs == DIMOFS_Y)
        {
            LONG dy = (LONG)e.dwData;
            if (gInvertY) dy = -dy;
            gAimY += (float)dy * gMouseMulY;
        }
    }

    return hr;
}

static void HookMouseDevice(LPDIRECTINPUTDEVICEA dev)
{
    if (!dev) return;

    void** vtbl = *(void***)dev;
    if (!vtbl) return;

    gMouseDevice = dev;
    if (!Real_SetProperty && SurvivorPc_MouseBufferSize())
    {
        void* pSetProperty = vtbl[VTI_DID_SetProperty];
        if (MH_CreateHook(pSetProperty, Hook_SetProperty, (void**)&Real_SetProperty) == MH_OK &&
            MH_EnableHook(pSetProperty) == MH_OK)
            LogLine("[DI] Hooked IDirectInputDeviceA::SetProperty");
    }

    // Hook GetDeviceData once per process
    if (!Real_GetDeviceData)
    {
        void* pGetDeviceData = vtbl[VTI_DID_GetDeviceData];

        if (MH_CreateHook(pGetDeviceData, Hook_GetDeviceData, (void**)&Real_GetDeviceData) == MH_OK &&
            MH_EnableHook(pGetDeviceData) == MH_OK)
        {
            LogLine("[DI] Hooked IDirectInputDeviceA::GetDeviceData");
        }
        else
        {
            LogLine("[DI] Failed to hook GetDeviceData (idx=%d)", VTI_DID_GetDeviceData);
        }
    }
}

static HRESULT STDMETHODCALLTYPE Hook_DI_CreateDevice(
    LPDIRECTINPUTA This, REFGUID rguid, LPDIRECTINPUTDEVICEA* lplpDirectInputDevice, LPUNKNOWN pUnkOuter)
{
    HRESULT hr = Real_DI_CreateDevice(This, rguid, lplpDirectInputDevice, pUnkOuter);

    if (SUCCEEDED(hr) && lplpDirectInputDevice && *lplpDirectInputDevice)
    {
        // We only care about SysMouse
        if (IsEqualGUID(rguid, GUID_SysMouse))
        {
            LogLine("[DI] CreateDevice(SysMouse) -> hooking mouse device");
            HookMouseDevice(*lplpDirectInputDevice);
        }
    }
    return hr;
}

static void HookDirectInputInterface(LPDIRECTINPUTA di)
{
    if (!di) return;
    void** vtbl = *(void***)di;
    if (!vtbl) return;

    // Hook CreateDevice on the DI interface (vtable patch via MinHook)
    if (!Real_DI_CreateDevice)
    {
        void* pCreateDevice = vtbl[VTI_DI_CreateDevice];
        if (MH_CreateHook(pCreateDevice, Hook_DI_CreateDevice, (void**)&Real_DI_CreateDevice) == MH_OK &&
            MH_EnableHook(pCreateDevice) == MH_OK)
        {
            LogLine("[DI] Hooked IDirectInputA::CreateDevice");
        }
        else
        {
            LogLine("[DI] Failed to hook IDirectInputA::CreateDevice");
        }
    }
}

static HRESULT WINAPI Hook_DirectInputCreateA(
    HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA* ppDI, LPUNKNOWN punkOuter)
{
    HRESULT hr = Real_DirectInputCreateA(hinst, dwVersion, ppDI, punkOuter);
    if (SUCCEEDED(hr) && ppDI && *ppDI)
    {
        LogLine("[DI] DirectInputCreateA ok (ver=%lu) -> hook DI", dwVersion);
        HookDirectInputInterface(*ppDI);
    }
    return hr;
}

extern "C" __declspec(dllexport) void InstallDInputHook()
{
    // MinHook init (safe to call multiple times)
    MH_Initialize();

    HMODULE mod = GetModuleHandleA("dinput.dll");
    if (!mod) mod = LoadLibraryA("dinput.dll");
    if (!mod)
    {
        LogLine("[DI] dinput.dll not found");
        return;
    }

    void* p = (void*)GetProcAddress(mod, "DirectInputCreateA");
    if (!p)
    {
        LogLine("[DI] DirectInputCreateA export not found");
        return;
    }

    if (MH_CreateHook(p, Hook_DirectInputCreateA, (void**)&Real_DirectInputCreateA) == MH_OK &&
        MH_EnableHook(p) == MH_OK)
    {
        LogLine("[DI] Hooked DirectInputCreateA");
    }
    else
    {
        LogLine("[DI] Failed to hook DirectInputCreateA");
    }
}
