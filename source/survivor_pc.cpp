// PC-side fixes ported from Survivor Fix (github.com/Trigger117, MIT licence), tested on
// gun_bio.exe SHA-256 2e5b7a3d...be3f9 (image base 0x00400000) at 2560x1440 with Windows scaling 125%.
//
// [SurvivorFix] in config.ini:
//   DpiAware=1        The process uses real screen pixels. Without it, Windows scaling (e.g. 125%) makes the
//                     window larger than the screen and the cursor positions do not match.
//   KeepAspect=1      The window keeps the aspect of ForceWidth x ForceHeight when maximised or resized
//                     (work area, centred), so the image is never stretched by the window.
//   SyncGameAim=1     The game's own aim ([0x5FD0BC]+0x2D8/+0x2DC, used for the shot and the menu cursor)
//                     follows the overlay crosshair every frame. The two positions used to drift apart after
//                     Alt+Tab (events skipped while unfocused, different clamps), so the shot missed the
//                     crosshair until the mouse was pushed into a corner.
//   EqualMouseAxes=1  The back buffer (1024x768) is stretched to ForceWidth x ForceHeight: at 2560x1440 a unit
//                     is 2.5 pixels across but 1.875 down. Vertical deltas are scaled by
//                     (outW/imageW)/(outH/imageH), so the mouse moves at the same on-screen speed on both axes.
//   MouseSensitivity=1.0  Multiplier for both axes of the overlay crosshair.
//   MenuWindowsCursor=1  In menus the cursor follows the Windows pointer (Windows speed and acceleration);
//                     in gameplay the crosshair keeps the raw mouse x MouseSensitivity. Gameplay is
//                     [0x5FD0CC]+0x41 == 3 (the in-game HUD draws only then, 0042C160); anything else is a menu.
//   HideCrosshairInMenus=1  The overlay crosshair is drawn only in gameplay, as in Classic Rebirth 1.0.
//   HudAspect=1       The in-game HUD keeps its 4:3 proportions on wide screens. The game builds it once in
//                     0042ABE0: four one-quad vertex buffers (0x70 bytes, FVF 0x144 XYZRHW|DIFFUSE|TEX1) in
//                     1024x768 space: WEAPON+BULLET x 0-819.2, CONDITION x 793.6-1023, muzzle flash x 310-986.
//                     While it runs, each quad's x is squeezed by (1024/768)/(outW/outH) at Unlock, around the
//                     left edge, the right edge (1023) or the centre (the quad touches neither edge).
//   MouseBufferSize=1024  The game asks for a 16-event mouse buffer and loses most of a modern mouse's
//                     events; the buffer gets at least this size (dinputhook.cpp, SetProperty).
//   Ps1ShadowBlend=1  Enemy shadows blend like the PS1 version. PS1 (TEST.BIN 80073C5C): two POLY_FT4 with
//                     colour 0x20, SetSemiTrans(1) and GetTPage(0, 0, 0x278, 0x170), i.e. semi-transparency
//                     mode 0: pixel = background/2 + (texel * 0x20/0x80)/2, texel 0 transparent. The PC
//                     (0043CB74-0043CC8C) draws SHADOW.BMP (a 17,17,17 disc on colour-keyed black) opaque with
//                     alpha test (ALPHABLENDENABLE 0, ALPHAREF 1, GREATEREQUAL), a near-black disc. Its draw is
//                     the DrawPrimitive after SetRenderState(ALPHABLENDENABLE, 0) returning to 0043CBD8; only
//                     that draw gets src*a + dst*(1-a) with a = texture alpha * 0x80 and colour = texture *
//                     0x40 (TEXTUREFACTOR 0x80404040). The alpha test keeps the colour-keyed edge.
#include "survivor_pc.h"
#include "survivor_fix/ps1/title_fade_logic.h"

#include "d3d8types.hpp"  // D3DSURFACE_DESC8, as in dllmain.cpp
#include "d3d8.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "MinHook.h"

namespace {

bool gDpiAware = false;
bool gKeepAspect = false;
bool gSyncGameAim = false;
bool gEqualMouseAxes = false;
float gSensitivity = 1.0f;
unsigned long gMouseBufferSize = 0;
bool gMenuWindowsCursor = false;
bool gHideCrosshairInMenus = false;
bool gHudAspect = false;
bool gPs1ShadowBlend = false;
bool gSmoothModelLighting = true;
int gOutW = 0, gOutH = 0;  // window client size (ForceWidth x ForceHeight)
float gAxisScaleY = 1.0f;  // EqualMouseAxes, set in SurvivorPc_OnDevice

// gun_bio.exe addresses (verified in Ghidra, see Survivor Fix src/game.h).
constexpr uintptr_t kInputObject = 0x005FD0BC;  // pointer to the input object
constexpr size_t kAimOffset = 0x2D8;            // int x, int y: the game's aim (FUN_00407050 adds mouse deltas)
constexpr uintptr_t kGameWidth = 0x005FD020;    // int: the game's video mode (0040E160), 1024
constexpr uintptr_t kGameHeight = 0x005FD024;   // int: 768
constexpr uintptr_t kGameObject = 0x005FD0CC;   // pointer to the game object
constexpr size_t kGameMode = 0x41;              // byte: 3 = gameplay (HUD drawn), 4/6 = no menu cursor (00407700)
// Inside FUN_00407050 (the game adds mouse deltas to its aim), past the prologue that Survivor Fix hooks:
// 00407096 MOV EAX,[0x5FD020] / MOV EDX,[ECX+0x2D8]. Checked before touching game memory.
constexpr uintptr_t kMoveAim = 0x00407096;
constexpr uint8_t kMoveAimBytes[] = {0xA1, 0x20, 0xD0, 0x5F, 0x00, 0x8B, 0x91, 0xD8, 0x02, 0x00, 0x00};

bool IsSurvivorExe() {
  static int ok = -1;
  if (ok < 0) {
    ok = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) == 0x00400000 &&
         memcmp(reinterpret_cast<const void*>(kMoveAim), kMoveAimBytes, sizeof kMoveAimBytes) == 0;
  }
  return ok != 0;
}

// ---- KeepAspect -------------------------------------------------------------------------------------
HWND gWindow = nullptr;
WNDPROC gPrevProc = nullptr;
bool gUnicode = false;
double gAspect = 0.0;  // ForceWidth / ForceHeight

struct Size {
  int w, h;
};

// Largest client size with the given aspect inside max.
Size Fit(Size max, double aspect) {
  if (aspect <= 0.0 || max.w <= 0 || max.h <= 0) return max;
  Size s{static_cast<int>(max.h * aspect + 0.5), max.h};
  if (s.w > max.w) s = {max.w, static_cast<int>(max.w / aspect + 0.5)};
  return s;
}

// Frame thickness (borders and caption) for the window style.
SIZE Frame(HWND h) {
  RECT r{0, 0, 100, 100};
  AdjustWindowRectEx(&r, static_cast<DWORD>(GetWindowLongW(h, GWL_STYLE)), FALSE,
                     static_cast<DWORD>(GetWindowLongW(h, GWL_EXSTYLE)));
  return {(r.right - r.left) - 100, (r.bottom - r.top) - 100};
}

// Work area of the monitor, or the whole monitor for a borderless window.
bool Area(HWND h, RECT& area) {
  MONITORINFO mi{sizeof mi};
  if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return false;
  area = (GetWindowLongW(h, GWL_STYLE) & WS_CAPTION) ? mi.rcWork : mi.rcMonitor;
  return true;
}

// Keeps the client area in aspect and on screen; a window larger than the screen is shrunk and centred.
void ClampWindowPos(HWND h, WINDOWPOS* pos) {
  if ((pos->flags & SWP_NOSIZE) || IsZoomed(h) || IsIconic(h)) return;
  RECT area;
  if (!Area(h, area)) return;
  const SIZE f = Frame(h);
  const Size max{(area.right - area.left) - f.cx, (area.bottom - area.top) - f.cy};
  const Size client{pos->cx - f.cx, pos->cy - f.cy};
  if (client.w <= 0 || client.h <= 0) return;
  const bool tooBig = client.w > max.w || client.h > max.h;
  const Size fit = Fit({client.w < max.w ? client.w : max.w, client.h < max.h ? client.h : max.h}, gAspect);
  if (fit.w == client.w && fit.h == client.h) return;
  pos->cx = fit.w + f.cx;
  pos->cy = fit.h + f.cy;
  if (tooBig) {
    pos->x = area.left + ((area.right - area.left) - pos->cx) / 2;
    pos->y = area.top + ((area.bottom - area.top) - pos->cy) / 2;
    pos->flags &= ~SWP_NOMOVE;
  }
}

LRESULT CALLBACK AspectProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_WINDOWPOSCHANGING) ClampWindowPos(h, reinterpret_cast<WINDOWPOS*>(l));
  const LRESULT result =
      gUnicode ? CallWindowProcW(gPrevProc, h, m, w, l) : CallWindowProcA(gPrevProc, h, m, w, l);
  if (m == WM_GETMINMAXINFO) {
    // Maximised: the work area (without the taskbar) plus the borders left off screen, client in aspect, centred.
    auto* info = reinterpret_cast<MINMAXINFO*>(l);
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return result;
    const SIZE f = Frame(h);
    const int border = info->ptMaxPosition.x < 0 ? -info->ptMaxPosition.x : 0;
    const Size full{(mi.rcWork.right - mi.rcWork.left) + 2 * border - f.cx,
                    (mi.rcWork.bottom - mi.rcWork.top) + 2 * border - f.cy};
    const Size fit = Fit(full, gAspect);
    info->ptMaxPosition.x = (mi.rcWork.left - mi.rcMonitor.left) - border + (full.w - fit.w) / 2;
    info->ptMaxPosition.y = (mi.rcWork.top - mi.rcMonitor.top) - border + (full.h - fit.h) / 2;
    info->ptMaxSize.x = fit.w + f.cx;
    info->ptMaxSize.y = fit.h + f.cy;
  } else if (m == WM_SIZING) {
    auto* r = reinterpret_cast<RECT*>(l);
    const SIZE f = Frame(h);
    const bool keepWidth = w == WMSZ_LEFT || w == WMSZ_RIGHT || w == WMSZ_BOTTOMLEFT || w == WMSZ_BOTTOMRIGHT ||
                           w == WMSZ_TOPLEFT || w == WMSZ_TOPRIGHT;
    const int cw = (r->right - r->left) - f.cx, ch = (r->bottom - r->top) - f.cy;
    const Size c = keepWidth ? Size{cw, static_cast<int>(cw / gAspect + 0.5)}
                             : Size{static_cast<int>(ch * gAspect + 0.5), ch};
    if (w == WMSZ_TOP || w == WMSZ_TOPLEFT || w == WMSZ_TOPRIGHT) {
      r->top = r->bottom - (c.h + f.cy);
    } else {
      r->bottom = r->top + c.h + f.cy;
    }
    if (w == WMSZ_LEFT || w == WMSZ_TOPLEFT || w == WMSZ_BOTTOMLEFT) {
      r->left = r->right - (c.w + f.cx);
    } else {
      r->right = r->left + c.w + f.cx;
    }
    return TRUE;
  }
  return result;
}

void HookWindow(HWND hwnd) {
  if (gWindow == hwnd || !hwnd) return;
  gWindow = hwnd;
  gUnicode = IsWindowUnicode(hwnd) != FALSE;
  const LONG_PTR proc = reinterpret_cast<LONG_PTR>(&AspectProc);
  gPrevProc = reinterpret_cast<WNDPROC>(gUnicode ? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, proc)
                                                 : SetWindowLongPtrA(hwnd, GWLP_WNDPROC, proc));
  if (!gPrevProc) {
    gWindow = nullptr;
    return;
  }
  RECT r{};
  GetWindowRect(hwnd, &r);  // same size again: ClampWindowPos fixes it if needed
  SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}

}  // namespace

void SurvivorPc_LoadConfig(const char* ini) {
  auto has = [&](const char* section, const char* key) {
    char value[2] = {};
    return GetPrivateProfileStringA(section, key, nullptr, value, sizeof value, ini) != 0;
  };
  gDpiAware = GetPrivateProfileIntA("SurvivorFix", "DpiAware", 1, ini) != 0;
  gKeepAspect = GetPrivateProfileIntA(has("Window", "KeepAspectRatio") ? "Window" : "SurvivorFix",
                                      has("Window", "KeepAspectRatio") ? "KeepAspectRatio" : "KeepAspect", 1, ini) != 0;
  gSyncGameAim = GetPrivateProfileIntA("SurvivorFix", "SyncGameAim", 1, ini) != 0;
  gEqualMouseAxes = GetPrivateProfileIntA("SurvivorFix", "EqualMouseAxes", 1, ini) != 0;
  char buf[32] = {};
  GetPrivateProfileStringA(has("Crosshair", "MouseSensitivity") ? "Crosshair" : "SurvivorFix",
                           "MouseSensitivity", "1.0", buf, sizeof buf, ini);
  const float s = static_cast<float>(atof(buf));
  gSensitivity = (s > 0.0f && s < 20.0f) ? s : 1.0f;
  gMenuWindowsCursor = GetPrivateProfileIntA("SurvivorFix", "MenuWindowsCursor", 1, ini) != 0;
  gHideCrosshairInMenus = GetPrivateProfileIntA("SurvivorFix", "HideCrosshairInMenus", 1, ini) != 0;
  gHudAspect = GetPrivateProfileIntA("SurvivorFix", "HudAspect", 1, ini) != 0;
  gPs1ShadowBlend = GetPrivateProfileIntA("SurvivorFix", "Ps1ShadowBlend", 1, ini) != 0;
  gSmoothModelLighting = GetPrivateProfileIntA(has("Fixes", "SmoothModelLighting") ? "Fixes" : "SurvivorFix",
                                               "SmoothModelLighting", 1, ini) != 0;
  const UINT buffer = GetPrivateProfileIntA("SurvivorFix", "MouseBufferSize", 1024, ini);
  gMouseBufferSize = buffer > 65536 ? 65536 : buffer;
}

void SurvivorPc_ApplyDpiAware() {
  if (!gDpiAware) return;
  using ContextFn = BOOL(WINAPI*)(HANDLE);
  auto context = reinterpret_cast<ContextFn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext")));
  if (context && context(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)))) return;  // per monitor v2
  SetProcessDPIAware();
}

void SurvivorPc_OnDevice(HWND hwnd, int imageW, int imageH, int outW, int outH) {
  if (outW > 0 && outH > 0) {
    gAspect = static_cast<double>(outW) / outH;
    gOutW = outW;
    gOutH = outH;
  }
  if (gKeepAspect && gAspect > 0.0) HookWindow(hwnd);
  if (gEqualMouseAxes && imageW > 0 && imageH > 0 && outW > 0 && outH > 0) {
    gAxisScaleY = (static_cast<float>(outW) / imageW) / (static_cast<float>(outH) / imageH);
  }
}

void SurvivorPc_SyncGameAim(float x, float y) {
  if (!gSyncGameAim || !IsSurvivorExe() || !std::isfinite(x) || !std::isfinite(y)) return;
  auto* input = *reinterpret_cast<uint8_t**>(kInputObject);
  const int w = *reinterpret_cast<const int*>(kGameWidth);
  const int h = *reinterpret_cast<const int*>(kGameHeight);
  if (!input || w <= 0 || h <= 0) return;
  int* aim = reinterpret_cast<int*>(input + kAimOffset);
  const int ax = static_cast<int>(std::lround(x)), ay = static_cast<int>(std::lround(y));
  aim[0] = ax < 0 ? 0 : (ax > w - 1 ? w - 1 : ax);
  aim[1] = ay < 0 ? 0 : (ay > h - 1 ? h - 1 : ay);
}

float SurvivorPc_MouseMulX() { return gSensitivity; }
float SurvivorPc_MouseMulY() { return gSensitivity * gAxisScaleY; }
unsigned long SurvivorPc_MouseBufferSize() { return gMouseBufferSize; }

namespace {
bool InGameplay() {
  if (!IsSurvivorExe()) return true;
  const auto* game = *reinterpret_cast<const uint8_t* const*>(kGameObject);
  return game && game[kGameMode] == 3;
}
}  // namespace

bool SurvivorPc_UpdateCrosshair(HWND hwnd, float* x, float* y, int bbW, int bbH) {
  const bool gameplay = InGameplay();
  if (gMenuWindowsCursor && !gameplay && hwnd && GetForegroundWindow() == hwnd && bbW > 0 && bbH > 0) {
    POINT p{};
    RECT c{};
    if (GetCursorPos(&p) && ScreenToClient(hwnd, &p) && GetClientRect(hwnd, &c) && c.right > 0 && c.bottom > 0) {
      *x = static_cast<float>(p.x) * bbW / c.right;
      *y = static_cast<float>(p.y) * bbH / c.bottom;
    }
  }
  return gameplay || !gHideCrosshairInMenus;
}

// ---- HudAspect ----------------------------------------------------------------------------------------
namespace {

constexpr uintptr_t kBuildHud = 0x0042ABE0;  // __fastcall(hud): builds the in-game HUD quads once
constexpr uint8_t kBuildHudBytes[] = {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xFF, 0x68, 0x8C, 0x88, 0x5A, 0x00};
constexpr UINT kQuadBytes = 0x70;     // 4 vertices
constexpr UINT kVertexBytes = 28;     // XYZRHW (4 floats), diffuse, u, v
constexpr DWORD kHudFvf = 0x144;      // D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1

using BuildHudFn = void(__fastcall*)(void* hud, void* edx);
BuildHudFn gBuildHud = nullptr;
bool gHudBuilding = false;
void* gHudVb = nullptr;
BYTE* gHudData = nullptr;

void __fastcall BuildHudDetour(void* hud, void* edx) {
  gHudBuilding = true;
  gBuildHud(hud, edx);
  gHudBuilding = false;
  gHudVb = nullptr;
  gHudData = nullptr;
}

}  // namespace

void SurvivorPc_InstallGameHooks() {
  if (!gHudAspect || !IsSurvivorExe() ||
      memcmp(reinterpret_cast<const void*>(kBuildHud), kBuildHudBytes, sizeof kBuildHudBytes) != 0) {
    return;
  }
  MH_Initialize();  // safe to call more than once
  void* target = reinterpret_cast<void*>(kBuildHud);
  if (MH_CreateHook(target, reinterpret_cast<void*>(&BuildHudDetour), reinterpret_cast<void**>(&gBuildHud)) == MH_OK) {
    MH_EnableHook(target);
  }
}

bool SurvivorPc_HudBuilding() { return gHudBuilding; }

void SurvivorPc_OnHudVertexLock(void* vb, BYTE* data, UINT offset, UINT size, UINT bufferSize, DWORD fvf) {
  const bool wholeQuad = offset == 0 && (size == 0 || size == kQuadBytes) && bufferSize == kQuadBytes;
  gHudVb = (wholeQuad && fvf == kHudFvf) ? vb : nullptr;
  gHudData = gHudVb ? data : nullptr;
}

void SurvivorPc_OnHudVertexUnlock(void* vb) {
  if (!gHudBuilding || vb != gHudVb || !gHudData) return;
  BYTE* data = gHudData;
  gHudVb = nullptr;
  gHudData = nullptr;
  const int w = *reinterpret_cast<const int*>(kGameWidth);
  const int h = *reinterpret_cast<const int*>(kGameHeight);
  if (w <= 0 || h <= 0 || gOutW <= 0 || gOutH <= 0) return;
  const float scale = (static_cast<float>(w) / h) / (static_cast<float>(gOutW) / gOutH);
  if (scale > 0.999f) return;
  float minX = 1e9f, maxX = -1e9f;
  for (int v = 0; v < 4; ++v) {
    const float x = *reinterpret_cast<const float*>(data + kVertexBytes * v);
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
  }
  const float right = static_cast<float>(w - 1);  // the game puts the right edge at W - 1
  const float fixed = minX <= 0.5f ? 0.0f : (maxX >= right - 0.5f ? right : w / 2.0f);
  for (int v = 0; v < 4; ++v) {
    float* x = reinterpret_cast<float*>(data + kVertexBytes * v);
    *x = fixed + (*x - fixed) * scale;
  }
}

// ---- Ps1ShadowBlend -------------------------------------------------------------------------------------
namespace {
constexpr uintptr_t kShadowBlendOffReturn = 0x0043CBD8;  // after SetRenderState(ALPHABLENDENABLE, 0)
bool gShadowPending = false;
bool gShadowArmed = false;
}  // namespace

void SurvivorPc_ArmEnemyShadowDraw() { gShadowArmed = true; }

void SurvivorPc_OnSetRenderState(void* returnAddress, unsigned long state, unsigned long value) {
  if (!gPs1ShadowBlend) return;
  if (reinterpret_cast<uintptr_t>(returnAddress) == kShadowBlendOffReturn && state == 27 /* ALPHABLENDENABLE */ &&
      value == 0 && IsSurvivorExe()) {
    // The blood-pool path reaches this same state call, but skips the shadow
    // transform branch at 0043C903. Only a draw armed there is a shadow.
    gShadowPending = gShadowArmed;
    gShadowArmed = false;
  }
}

bool SurvivorPc_IsSurvivorExe() { return IsSurvivorExe(); }

bool SurvivorPc_SmoothModelLighting() { return gSmoothModelLighting && IsSurvivorExe(); }

bool SurvivorPc_TakeShadowDraw() {
  const bool pending = gShadowPending;
  gShadowPending = false;
  return pending;
}

// ---- PS1 screen effects (requested by Survivor Fix, see survivor_pc.h) -----------------------------------
namespace {

SurvivorScreenFx gFx{};
bool gScreenFxDrawSucceeded = true;
HRESULT gLastPresentResult = E_FAIL;
IDirect3DDevice8* gWrapper = nullptr;  // the device the game presents with
IDirect3DDevice8* gDev = nullptr;      // the device that draws (d3d8to9)
IDirect3DTexture8* gFrame = nullptr;   // copy of the previous frame (trails, held frame)
bool gFrameValid = false;
bool gTitleActive = false;
int gTitleLoadingBlackFrames = 0;
int gTitleBrightness = 128, gTitleFlash = 0;
bool gHolding = false;  // inside SurvivorPc_PresentHeldFrame: the held frame is not copied again
UINT gFrameW = 0, gFrameH = 0;
D3DFORMAT gFrameFormat = D3DFMT_UNKNOWN;

struct Vertex {
  float x, y, z, rhw;
  DWORD color;
  float u, v;
};
constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

int Clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

HRESULT Quad(IDirect3DDevice8* dev, float x0, float y0, float x1, float y1, DWORD color) {
  // Pixel centres: D3D8 samples texel centres half a pixel off.
  x0 -= 0.5f;
  y0 -= 0.5f;
  x1 -= 0.5f;
  y1 -= 0.5f;
  const Vertex v[4] = {
      {x0, y0, 0.0f, 1.0f, color, 0.0f, 0.0f},
      {x1, y0, 0.0f, 1.0f, color, 1.0f, 0.0f},
      {x0, y1, 0.0f, 1.0f, color, 0.0f, 1.0f},
      {x1, y1, 0.0f, 1.0f, color, 1.0f, 1.0f},
  };
  const HRESULT result=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex));
  if(FAILED(result))gScreenFxDrawSucceeded=false;
  return result;
}

// Untextured box: dest + colour (add) or dest - colour (subtract), per channel, clamped like the PS1 GPU.
void Box(IDirect3DDevice8* dev, float w, float h, DWORD color, bool subtract) {
  dev->SetTexture(0, nullptr);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
  dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
  dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
  dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
  dev->SetRenderState(D3DRS_BLENDOP, subtract ? D3DBLENDOP_REVSUBTRACT : D3DBLENDOP_ADD);
  Quad(dev, 0.0f, 0.0f, w, h, color);
}

void SetSampler(IDirect3DDevice8* dev, DWORD type, DWORD value) {
  dev->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(type), value);
}

// States shared by every draw here; the caller restores the game's with a state block.
void BeginDraw(IDirect3DDevice8* dev, UINT w, UINT h) {
  D3DVIEWPORT8 full{0, 0, w, h, 0.0f, 1.0f};
  dev->SetViewport(&full);
  dev->SetVertexShader(kFvf);
  dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
  dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
  dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
  // PS1 sprites are not filtered.
  SetSampler(dev, D3DTSS_MAGFILTER, D3DTEXF_POINT);
  SetSampler(dev, D3DTSS_MINFILTER, D3DTEXF_POINT);
  SetSampler(dev, D3DTSS_MIPFILTER, D3DTEXF_NONE);
  SetSampler(dev, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
  SetSampler(dev, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
  dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
  dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
}

bool EnsureFrame(IDirect3DDevice8* dev, UINT w, UINT h, D3DFORMAT format) {
  if (gFrame && gFrameW == w && gFrameH == h && gFrameFormat == format) return true;
  SurvivorPc_ReleaseScreenFx();
  if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &gFrame))) {
    gFrame = nullptr;
    return false;
  }
  gFrameW = w;
  gFrameH = h;
  gFrameFormat = format;
  return true;
}

void CopyFrame(IDirect3DDevice8* dev, IDirect3DSurface8* back) {
  IDirect3DSurface8* level = nullptr;
  gFrameValid = false;
  if (gFrame && SUCCEEDED(gFrame->GetSurfaceLevel(0, &level)) && level) {
    gFrameValid = SUCCEEDED(dev->CopyRects(back, nullptr, 0, level, nullptr));
    level->Release();
  }
}

bool BackBuffer(IDirect3DDevice8* dev, IDirect3DSurface8** back, D3DSURFACE_DESC8* desc) {
  *back = nullptr;
  if (FAILED(dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, back)) || !*back) return false;
  if (FAILED((*back)->GetDesc(desc))) {
    (*back)->Release();
    *back = nullptr;
    return false;
  }
  return true;
}

}  // namespace

void SurvivorPc_SetScreenFx(const SurvivorScreenFx* fx) {
  if (!fx || fx->size != static_cast<int>(sizeof(SurvivorScreenFx))) return;
  // A new trail starts from the frame shown before it; a held frame stays while it is blended.
  if (!gTitleActive && fx->trail == 0 && !fx->keep && fx->heldBlend <= 0) gFrameValid = false;
  gFx = *fx;
}

bool SurvivorPc_ScreenFxHidesCrosshair() { return gTitleActive || gFx.noCrosshair != 0; }


void SurvivorPc_ReleaseScreenFx() {
  if (gFrame) gFrame->Release();
  gFrame = nullptr;
  gFrameValid = false;
  gFrameW = gFrameH = 0;
}

void SurvivorPc_DrawScreenFx(IDirect3DDevice8* wrapper, IDirect3DDevice8* dev) {
  gScreenFxDrawSucceeded=true;
  gWrapper = wrapper;
  gDev = dev;
  const SurvivorScreenFx fx = gTitleActive ? SurvivorScreenFx{} : gFx;
  const bool trail = fx.trail == 1 || fx.trail == 2;
  const bool held = fx.heldBlend > 0 && gFrameValid && gFrame;
  const bool loadingBlack = gTitleLoadingBlackFrames > 0;
  const bool draw = trail || held || fx.fade > 0 || fx.redFlash || fx.subtract > 0 || loadingBlack ||
                    (gTitleActive && (gTitleBrightness < 128 || gTitleFlash > 0));
  const bool copy = (trail || fx.keep || gTitleActive) && !gHolding;
  if (!dev || (!draw && !copy)) return;
  IDirect3DSurface8* back = nullptr;
  D3DSURFACE_DESC8 desc{};
  if (!BackBuffer(dev, &back, &desc)) {gScreenFxDrawSucceeded=false;return;}
  const float w = static_cast<float>(desc.Width), h = static_cast<float>(desc.Height);

  if (draw) {
    DWORD block = 0;
    const bool saved = SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &block));
    if(!saved) {gScreenFxDrawSucceeded=false;if(gTitleActive){back->Release();return;}}
    D3DVIEWPORT8 oldViewport{};
    dev->GetViewport(&oldViewport);
    if (SUCCEEDED(dev->BeginScene())) {
      BeginDraw(dev, desc.Width, desc.Height);
      dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
      if (held) {
        dev->SetTexture(0, gFrame);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);  // ALPHAARG1 = diffuse
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        Quad(dev, 0.0f, 0.0f, w, h, D3DCOLOR_ARGB(Clamp255(fx.heldBlend), 0xFF, 0xFF, 0xFF));
      }
      // 80085814 sorts the subtract box into the same ordering table slot after the two sprites, so the GPU
      // draws it first: the zoomed frame is added over the darkened scene (the PS1 stays white while it runs).
      if (fx.subtract > 0) {
        const int v = Clamp255(fx.subtract);
        Box(dev, w, h, D3DCOLOR_ARGB(0xFF, v, v, v), true);
        dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
      }
      if (trail && gFrameValid && gFrame) {
        dev->SetTexture(0, gFrame);
        if (fx.trail == 1) {
          // 0x42000000: background / 2 + texel / 2.
          dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
          dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
          dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
          dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
          Quad(dev, 0.0f, 0.0f, w, h, D3DCOLOR_ARGB(0x80, 0xFF, 0xFF, 0xFF));
        } else {
          // 0x52000000: background + texel * colour / 0x80, the sprite scaled around the screen centre.
          const int c = Clamp255(fx.trailColor * 2);
          const float s = fx.trailScale / 4096.0f;
          dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
          dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
          dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
          dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
          Quad(dev, w * 0.5f * (1.0f - s), h * 0.5f * (1.0f - s), w * 0.5f * (1.0f + s), h * 0.5f * (1.0f + s),
               D3DCOLOR_ARGB(0xFF, c, c, c));
        }
      }
      if (gTitleActive && gTitleBrightness < 128) {
        const int color=ps1::TitleShadeColor(gTitleBrightness);
        dev->SetTexture(0,nullptr);
        dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
        dev->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ZERO);
        dev->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR);
        dev->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        Quad(dev,0,0,w,h,D3DCOLOR_ARGB(255,color,color,color));
      }
      if (gTitleActive && gTitleFlash > 0)
        Box(dev,w,h,D3DCOLOR_ARGB(255,gTitleFlash,gTitleFlash,gTitleFlash),false);
      if (loadingBlack) Box(dev, w, h, D3DCOLOR_ARGB(0xFF, 0xFF, 0xFF, 0xFF), true);
      if (fx.redFlash) Box(dev, w, h, D3DCOLOR_ARGB(0xFF, 0x40, 0, 0), false);  // 0x70000000: + (FF, 0, 0) / 4
      if (fx.fade > 0) {
        const int v = Clamp255(fx.fade);
        Box(dev, w, h, D3DCOLOR_ARGB(0xFF, v, v, v), true);
      }
      if(FAILED(dev->EndScene()))gScreenFxDrawSucceeded=false;
    } else {
      gScreenFxDrawSucceeded=false;
    }
    if (saved) {
      if(FAILED(dev->ApplyStateBlock(block)))gScreenFxDrawSucceeded=false;
      dev->DeleteStateBlock(block);
    }
    dev->SetViewport(&oldViewport);
  }

  // The PS1 sprites read the frame shown before: keep this one for the next Present.
  if(copy){
    if(EnsureFrame(dev,desc.Width,desc.Height,desc.Format))CopyFrame(dev,back);
    if(!gFrameValid)gScreenFxDrawSucceeded=false;
  }
  back->Release();
  if (loadingBlack) --gTitleLoadingBlackFrames;
}

int SurvivorPc_PresentHeldFrame() {
  IDirect3DDevice8* dev = gDev;
  if (!dev || !gWrapper || !gFrame || !gFrameValid) return 0;
  IDirect3DSurface8* back = nullptr;
  D3DSURFACE_DESC8 desc{};
  if (!BackBuffer(dev, &back, &desc)) return 0;
  back->Release();
  DWORD block = 0;
  const bool saved = SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &block));
  if(!saved)return 0;
  D3DVIEWPORT8 oldViewport{};
  dev->GetViewport(&oldViewport);
  bool drawn=false;
  if (SUCCEEDED(dev->BeginScene())) {
    BeginDraw(dev, desc.Width, desc.Height);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetTexture(0, gFrame);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    const HRESULT draw=Quad(dev,0.0f,0.0f,static_cast<float>(desc.Width),static_cast<float>(desc.Height),0xFFFFFFFF);
    const HRESULT end=dev->EndScene();
    drawn=SUCCEEDED(draw)&&SUCCEEDED(end);
  }
  const bool restored=SUCCEEDED(dev->ApplyStateBlock(block));
  dev->DeleteStateBlock(block);
  dev->SetViewport(&oldViewport);
  if(!drawn||!restored)return 0;
  gHolding = true;
  gLastPresentResult=E_FAIL;
  const HRESULT shown=gWrapper->Present(nullptr,nullptr,nullptr,nullptr);
  gHolding = false;
  // Classic Rebirth masks failed native Presents to keep the game alive. Use
  // the result reported before that mask, together with both draw passes.
  return SUCCEEDED(shown)&&SUCCEEDED(gLastPresentResult)&&gScreenFxDrawSucceeded ? 1 : 0;
}

bool SurvivorPc_BeginTitleTransition() {
  if(!gDev || !gWrapper || FAILED(gDev->TestCooperativeLevel())) return false;
  gTitleActive=true;gTitleBrightness=128;gTitleFlash=0;
  return true;
}
void SurvivorPc_SetTitleTransition(int brightness,int flash) {
  gTitleBrightness=(std::clamp)(brightness,0,128);gTitleFlash=Clamp255(flash);
}
bool SurvivorPc_TitleFrameReady() {
  return gTitleActive && gFrameValid && gFrame && gDev && gWrapper && SUCCEEDED(gDev->TestCooperativeLevel());
}
void SurvivorPc_EndTitleTransition() {
  gTitleActive=false;gTitleBrightness=128;gTitleFlash=0;gFrameValid=false;
}
void SurvivorPc_HoldTitleBlackForLoading(int presents) {
  gTitleLoadingBlackFrames = (std::max)(0, presents);
}

void SurvivorPc_OnPresentResult(HRESULT result) { gLastPresentResult=result; }
