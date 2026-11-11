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
#include "survivor_fix/survivor_pc.h"
#include "survivor_fix/xinput_logic.h"
#include "survivor_fix/features/features.h"

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
// We’ll use the standard DI7 layout index for GetDeviceData (works in most builds).
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
static thread_local unsigned gMouseMotionEvents = 0;
static volatile LONG gMouseLeftPressed = 0;
void SurvivorPc_ResetMouseRead() { gMouseMotionEvents = 0; }
unsigned SurvivorPc_MouseMotionEvents() { return gMouseMotionEvents; }
bool SurvivorPc_ConsumeMouseLeftPress() {
    return InterlockedExchange(&gMouseLeftPressed, 0) != 0;
}


// -------------------- XInput pad --------------------
// A pad is read once per keyboard poll and ORed into the keyboard state the game already
// reads, so pad and keyboard stay usable together and the game keeps its own input code.
// XInput is loaded dynamically: the game must not gain a hard dependency on it, and the
// feature stays idle when no pad is connected.
using XInputGetState_t = DWORD(WINAPI*)(DWORD, void*);
static XInputGetState_t g_xinputGetState = nullptr;
static bool g_xinputLookedUp = false;
static DWORD g_padIndex = 0;
static unsigned g_padRescanFrames = 0;
static LPDIRECTINPUTDEVICEA gKeyboardDevice = nullptr;
static uint32_t g_padActionsHeld = 0;  // actions held by the previous poll, for edge detection

static constexpr int VTI_DID_GetDeviceState = 9;  // DI7A: Acquire=7, Unacquire=8, GetDeviceState=9
using DID_GetDeviceState_t = HRESULT(STDMETHODCALLTYPE*)(LPDIRECTINPUTDEVICEA, DWORD, LPVOID);
static DID_GetDeviceState_t Real_GetDeviceState = nullptr;

// XINPUT_STATE / XINPUT_GAMEPAD, declared locally so the wrapper needs no XInput headers.
#pragma pack(push, 1)
struct PadGamepad
{
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};
struct PadStateRaw
{
    DWORD dwPacketNumber;
    PadGamepad Gamepad;
};
#pragma pack(pop)
static_assert(sizeof(PadGamepad) == 12, "XINPUT_GAMEPAD layout");
static_assert(sizeof(PadStateRaw) == 16, "XINPUT_STATE layout");

static XInputGetState_t LoadXInput()
{
    if (g_xinputLookedUp) return g_xinputGetState;
    g_xinputLookedUp = true;
    const char* candidates[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (const char* name : candidates)
    {
        HMODULE mod = LoadLibraryA(name);
        if (!mod) continue;
        g_xinputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(mod, "XInputGetState"));
        if (g_xinputGetState)
        {
            LogLine("[PAD] XInputGetState from %s", name);
            break;
        }
    }
    if (!g_xinputGetState) LogLine("[PAD] XInput not available; pad support idle");
    return g_xinputGetState;
}

static float PadAxis(SHORT value)
{
    const float scaled = static_cast<float>(value) / 32767.0f;
    return scaled < -1.0f ? -1.0f : (scaled > 1.0f ? 1.0f : scaled);
}

// Reads the first connected pad. A pad that drops out is looked for again, but only
// every 30 polls, so an empty socket costs nothing per frame.
static bool PollPad(sf::pad::PadState* out)
{
    XInputGetState_t get = LoadXInput();
    if (!get || !out) return false;
    PadStateRaw raw = {};
    if (get(g_padIndex, &raw) != ERROR_SUCCESS)
    {
        if (g_padRescanFrames++ < 30) return false;
        g_padRescanFrames = 0;
        bool found = false;
        for (DWORD i = 0; i < 4; ++i)
        {
            if (i == g_padIndex) continue;
            PadStateRaw candidate = {};
            if (get(i, &candidate) == ERROR_SUCCESS)
            {
                g_padIndex = i;
                raw = candidate;
                found = true;
                LogLine("[PAD] Using controller %lu", i);
                break;
            }
        }
        if (!found) return false;
    }
    out->buttons = raw.Gamepad.wButtons;
    out->leftX = PadAxis(raw.Gamepad.sThumbLX);
    out->leftY = PadAxis(raw.Gamepad.sThumbLY);
    out->rightX = PadAxis(raw.Gamepad.sThumbRX);
    out->rightY = PadAxis(raw.Gamepad.sThumbRY);
    out->leftTrigger = raw.Gamepad.bLeftTrigger;
    out->rightTrigger = raw.Gamepad.bRightTrigger;
    return true;
}

// The right stick moves the overlay crosshair, which the game's aim follows (EndScene syncs it).
static void ApplyPadAim(const sf::pad::PadState& pad, float deadZone)
{
    const float speed = SurvivorPc_PadAimSpeed();
    if (speed <= 0.0f) return;
    gAimX += sf::pad::StickAimDelta(pad.rightX, deadZone, speed);
    // XInput reports up as positive and the crosshair's y grows downwards.
    const float vertical = sf::pad::StickAimDelta(pad.rightY, deadZone, speed);
    gAimY += SurvivorPc_PadAimInvertY() ? vertical : -vertical;
}

// Bindings with no game key run the wrapper's own features. Only a fresh press counts, so
// holding a button does not toggle the debug menu on every poll.
static void DispatchPadActions(uint32_t actions)
{
    const uint32_t pressed = actions & ~g_padActionsHeld;
    g_padActionsHeld = actions;
    if (!pressed) return;
    if (pressed & (1u << static_cast<unsigned>(sf::pad::Action::DebugMenu))) sf::features::ToggleDebugMenu();
    if (pressed & (1u << static_cast<unsigned>(sf::pad::Action::QuickSave))) sf::features::QuickSaveHotkey(true);
    if (pressed & (1u << static_cast<unsigned>(sf::pad::Action::QuickLoad))) sf::features::QuickSaveHotkey(false);
    if (pressed & (1u << static_cast<unsigned>(sf::pad::Action::Fullscreen))) sf::features::ToggleFullscreenWindow();
}

static HRESULT STDMETHODCALLTYPE Hook_GetDeviceState(LPDIRECTINPUTDEVICEA This, DWORD cbData, LPVOID lpvData)
{
    HRESULT hr = Real_GetDeviceState(This, cbData, lpvData);
    if (FAILED(hr) || This != gKeyboardDevice || !lpvData) return hr;
    if (!SurvivorPc_XInputEnabled() || !IsOurWindowFocused())
    {
        g_padActionsHeld = 0;
        return hr;
    }
    sf::pad::PadState pad;
    if (!PollPad(&pad))
    {
        g_padActionsHeld = 0;
        return hr;
    }
    const float deadZone = SurvivorPc_XInputDeadZone();
    const sf::pad::Binding* bindings = SurvivorPc_PadBindings();
    sf::pad::OverlayBoundKeys(pad, bindings, sf::pad::kSourceCount, deadZone,
                              static_cast<uint8_t*>(lpvData), cbData);
    ApplyPadAim(pad, deadZone);
    DispatchPadActions(sf::pad::ActionMask(pad, bindings, sf::pad::kSourceCount, deadZone));
    return hr;
}

static void HookKeyboardDevice(LPDIRECTINPUTDEVICEA dev)
{
    if (!dev || Real_GetDeviceState) return;
    void** vtbl = *(void***)dev;
    if (!vtbl) return;
    gKeyboardDevice = dev;
    void* pGetDeviceState = vtbl[VTI_DID_GetDeviceState];
    if (MH_CreateHook(pGetDeviceState, Hook_GetDeviceState, (void**)&Real_GetDeviceState) == MH_OK &&
        MH_EnableHook(pGetDeviceState) == MH_OK)
    {
        LogLine("[PAD] Hooked IDirectInputDeviceA::GetDeviceState (pad overlays the keyboard)");
    }
    else
    {
        LogLine("[PAD] Failed to hook GetDeviceState (idx=%d)", VTI_DID_GetDeviceState);
        gKeyboardDevice = nullptr;
    }
}

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

    // The method implementation may be shared by keyboard devices; only consume actual mouse data.
    // A peek must not move the overlay or count the same movement twice.
    if (This != gMouseDevice || (dwFlags & DIGDD_PEEK)) return hr;
    if (cbObjectData == 16 || cbObjectData == sizeof(DIDEVICEOBJECTDATA)) {
        const auto* data = reinterpret_cast<const unsigned char*>(rgdod);
        for (DWORD i = 0; i < *pdwInOut; ++i) {
            DWORD offset = 0;
            DWORD value = 0;
            std::memcpy(&offset, data + i * cbObjectData, sizeof(offset));
            std::memcpy(&value, data + i * cbObjectData + sizeof(DWORD), sizeof(value));
            if (offset == DIMOFS_X || offset == DIMOFS_Y) ++gMouseMotionEvents;
            if (offset == DIMOFS_BUTTON0 && (value & 0x80))
                InterlockedExchange(&gMouseLeftPressed, 1);
        }
    }

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
        else if (IsEqualGUID(rguid, GUID_SysKeyboard))
        {
            LogLine("[DI] CreateDevice(SysKeyboard) -> hooking keyboard device");
            HookKeyboardDevice(*lplpDirectInputDevice);
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
