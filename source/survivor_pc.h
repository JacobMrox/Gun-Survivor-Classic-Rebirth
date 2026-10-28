// PC-side fixes ported from Survivor Fix (github.com/Trigger117, MIT licence).
// Fixes default to on; diagnostic overrides live in config.ini, section [SurvivorFix].
#pragma once
#include <windows.h>

// Reads [SurvivorFix] from config.ini. Call once from DllMain, before the other functions.
void SurvivorPc_LoadConfig(const char* iniPath);

// DpiAware: from DllMain, before the game creates its window.
void SurvivorPc_ApplyDpiAware();

// After CreateDevice/Reset: KeepAspect window handling and the mouse axis scale.
// imageW x imageH is the back buffer (the game's own video mode), outW x outH the window client size.
void SurvivorPc_OnDevice(HWND hwnd, int imageW, int imageH, int outW, int outH);

// SyncGameAim: from EndScene, after the overlay crosshair is clamped (back buffer space).
void SurvivorPc_SyncGameAim(float x, float y);

// Multipliers for the mouse deltas that move the overlay crosshair (dinputhook.cpp).
float SurvivorPc_MouseMulX();
float SurvivorPc_MouseMulY();

// MouseBufferSize: minimum DirectInput buffer for the mouse (0 = leave the game's 16).
unsigned long SurvivorPc_MouseBufferSize();
// Movement events seen by the existing DirectInput bridge in one native device read.
void SurvivorPc_ResetMouseRead();
unsigned SurvivorPc_MouseMotionEvents();

// From EndScene, before the overlay crosshair is clamped and drawn. MenuWindowsCursor: in menus the crosshair
// position (and so the game's aim) comes from the Windows pointer, in back buffer space. Returns false when the
// overlay crosshair should not be drawn (HideCrosshairInMenus: the game draws its own menu cursor).
bool SurvivorPc_UpdateCrosshair(HWND hwnd, float* x, float* y, int bbW, int bbH);

// HudAspect: from DllMain, hooks the game function that builds the in-game HUD (gun_bio 0042ABE0).
void SurvivorPc_InstallGameHooks();
// True while 0042ABE0 runs; the vertex buffer wrapper then reports its Lock/Unlock.
bool SurvivorPc_HudBuilding();
void SurvivorPc_OnHudVertexLock(void* vb, BYTE* data, UINT offset, UINT size, UINT bufferSize, DWORD fvf);
void SurvivorPc_OnHudVertexUnlock(void* vb);

// Ps1ShadowBlend: SetRenderState reports its caller; the next DrawPrimitive after the shadow's
// SetRenderState(ALPHABLENDENABLE, 0) at 0043CBD2 is drawn with the PS1 blend (survivor_pc.cpp).
void SurvivorPc_OnSetRenderState(void* returnAddress, unsigned long state, unsigned long value);
// Armed only by the living-enemy shadow branch. Blood pools skip that branch
// and must retain their native red texture/blending.
void SurvivorPc_ArmEnemyShadowDraw();
void SurvivorPc_ArmEnemyPoolDraw(int texture);
// 0 none, 1 shadow, 2 BLOODX1, 3 BLOODX2.
int SurvivorPc_TakeEnemySurfaceDraw();

// True when the running executable is the PC port of Resident Evil Survivor (gun_bio.exe, checked by bytes).
bool SurvivorPc_IsSurvivorExe();
// Experimental fixed-function lighting correction. Survivor's model normals
// are fixed-point scaled; D3D must normalize them before diffuse lighting.
bool SurvivorPc_SmoothModelLighting();

// ---- PS1 screen effects ----------------------------------------------------------------------------------
// Screen effects of the PS1 version of Resident Evil Survivor (fades, the blurred view, the death zoom, the red
// flash), drawn over the finished frame just before Present. survivor_fix/features/screen_fx.cpp runs the PS1 timing
// and sets the state through SurvivorPc_SetScreenFx; nothing is drawn while it is off.
// PS1 references (SLUS_010.87, TEST.BIN loaded at 0x8006C118):
//   fade     80090D4C: box fill with attribute 0x60000000 (subtract) and colour = level (80090C90, 0..255).
//   trail 1  80085418: the previous frame as GsSPRITE 0x42000000 (blend 50/50), colour 0x80, scale 1.
//   trail 2  80085814: the previous frame as GsSPRITE 0x52000000 (add), colour bb158, scale bb164 (4096 = 1),
//            centred (x = -160 * scale, y = -120 * scale from the screen centre).
//   red      80082608: box fill 0x70000000 (background + colour / 4) with colour (255, 0, 0).
//   subtract 80085814: box fill bb170, 0x60000000 (subtract), colour bb15c.
struct IDirect3DDevice8;

struct SurvivorScreenFx {
  int size;        // sizeof(SurvivorScreenFx)
  int fade;        // 0..255, subtracted from every channel, drawn last
  int trail;       // 0 none, 1 previous frame at 50%, 2 previous frame added and zoomed
  int trailColor;  // trail 2: 0..255, 128 = texel unchanged (PS1 texture modulation)
  int trailScale;  // trail 2: 4096 = 1.0
  int redFlash;    // non-zero: (64, 0, 0) added
  int subtract;    // 0..255, subtracted before the trail is added
  int keep;        // non-zero: keep a copy of every frame for SurvivorPc_PresentHeldFrame
  int noCrosshair; // non-zero: the overlay crosshair is not drawn (the PS1 has none in these scenes)
  int heldBlend;   // 0..255: the kept frame is drawn over the game's with this opacity (it stops being copied when
                   // keep is 0). GAME OVER over the Continue picture: the PS1 brightens only the words (8007B2A8),
                   // the game composes them once, so the picture without them fades out over them.
};

void SurvivorPc_SetScreenFx(const SurvivorScreenFx* fx);
// Shows the last kept frame again, with the current effects over it, and presents it. For the PS1 fade
// before a door (8009016C step 1: 80090BF0(0, 0x10) over the room) once the game has left the room.
// Returns 0 when there is no kept frame or no device.
int SurvivorPc_PresentHeldFrame();

// From Present, before the swap: `wrapper` is the device the game uses (for SurvivorPc_PresentHeldFrame),
// `dev` the one to draw with. Keeps a copy of the frame while a trail is on or `keep` is set.
void SurvivorPc_DrawScreenFx(IDirect3DDevice8* wrapper, IDirect3DDevice8* dev);
// The overlay crosshair stays hidden (EndScene).
bool SurvivorPc_ScreenFxHidesCrosshair();
// Before Reset: the frame copy lives in D3DPOOL_DEFAULT.
void SurvivorPc_ReleaseScreenFx();

// Scoped title-only rendering. The native confirmation scene is finished once,
// then its kept frame is modulated by TITLE.BIN brightness/128 and flashed white.
bool SurvivorPc_BeginTitleTransition();
void SurvivorPc_SetTitleTransition(int brightness,int flash);
bool SurvivorPc_TitleFrameReady();
void SurvivorPc_EndTitleTransition();
// Keeps the native loading frames black after the title confirmation has completed its fade.
void SurvivorPc_HoldTitleBlackForLoading(int presents);

// Raw backend result before the wrapper masks failures for the native game.
void SurvivorPc_OnPresentResult(HRESULT result);
