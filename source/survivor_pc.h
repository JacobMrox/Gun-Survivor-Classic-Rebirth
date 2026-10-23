// PC-side fixes ported from Survivor Fix (github.com/Trigger117, MIT licence).
// Options live in config.ini, section [SurvivorFix]; every option defaults to off.
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
bool SurvivorPc_TakeShadowDraw();

// True when the running executable is the PC port of Resident Evil Survivor (gun_bio.exe, checked by bytes).
bool SurvivorPc_IsSurvivorExe();
