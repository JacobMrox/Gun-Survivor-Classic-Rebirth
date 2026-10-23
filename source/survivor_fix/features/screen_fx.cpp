// PS1 screen effects (ps1/screen_fx_logic.h): the player's death (Ps1PlayerDeath) and the door fades
// (Ps1DoorFade). The timing and the logic are here; the drawing (subtractive boxes, previous frame drawn
// over the new one, red flash) is done before Present by survivor_pc.cpp.
#include <windows.h>
#include <mmsystem.h>  // timeGetTime: the same clock as the game's screens

#include <cstdint>

#include "../../survivor_pc.h"
#include "../config.h"
#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "../ps1/screen_fx_logic.h"
#include "features.h"

#pragma comment(lib, "winmm.lib")

namespace sf::features {

namespace {

using EndFrameFn = int(__fastcall*)(void* game, void* edx, int a);
using ReadPadFn = uint32_t(__fastcall*)(void* game, void* edx, int a);
using MouseClickFn = int(__fastcall*)(void* mouse, void* edx, int a, int b, int c, int d, int e, int f);
using RoomTickFn = int(__fastcall*)(void* game, void* edx, uint32_t* pad, uint32_t* prev, int* turns, float p5,
                                    int* scene, int p7, float p8);
using DrawHudFn = void(__fastcall*)(void* hud, void* edx);
using DoorAnimFn = void(__fastcall*)(void* door, void* edx, const char* path, int kind);
using YouDiedFn = int(__fastcall*)(DWORD* screen, void* edx, int a);
using ContinueFn = int(__fastcall*)(DWORD* screen, void* edx, int a, char continues);
EndFrameFn g_endFrame = nullptr;
ReadPadFn g_readPad = nullptr;
MouseClickFn g_mouseClick = nullptr;
RoomTickFn g_roomTick = nullptr;
DrawHudFn g_drawHud = nullptr;
DoorAnimFn g_doorAnim = nullptr;
YouDiedFn g_youDied = nullptr;
ContinueFn g_continue = nullptr;

constexpr int kStateRooms = 6;
constexpr int kStateYouDied = 7;
constexpr int kStateContinue = 8;

ps1::Fade g_fade;
ps1::PlayerDeath g_death;
int g_deathPitch = 0;                 // 800A865C of the view: the steps of 8008369C and the return of 800789F8
int g_deathEyeStart = ps1::kEyeRest;  // 800A8634 at step 0 (with the bob: DuckStation -1642)
bool g_dying = false;                 // 8008369C steps 0-6: the room stays open
bool g_redFlash = false;
int g_lastState = -1;
int g_youDiedFrames = 0;  // 800BB074 in steps 8-9
int g_youDiedLoad = 0;    // black loading frames (step 7) still to go
bool g_youDiedLeaving = false;
bool g_injectConfirm = false;
bool g_confirmHeld = false;
bool g_roomTicked = false;  // a room frame (one PS1 frame) ran in this frame
int g_youDiedCredit = 0;    // PS1 frames of YOU DIED not yet given to the PC screen
int g_youDiedClock = 0;     // PS1 frames since the start of the YOU DIED fade
int g_youDiedPhaseStart = 0;
int g_youDiedLastPhase = -1;
int g_continueLoad = 0;     // black loading frames of the Continue (step 0xB) still to go
DWORD g_continueClock = 0;  // last timeGetTime seen by the Continue
int g_gameOver = -1;        // PS1 frames since the end of the countdown (phase 2 of the PC Continue), -1 = no
bool g_gameOverDone = false;
bool g_gameOverLoaded = false;  // the PC has loaded GAME OVER (first step of phase 2)

// Outside the rooms (menus, doors) the PC draws more often than the PS1: PS1 frames are counted by time,
// two NTSC VBlanks = 1 / 29.97 s.
LARGE_INTEGER g_frequency{};
LARGE_INTEGER g_clock{};
double g_pending = 0.0;
constexpr double kPs1FrameSeconds = 2.0 * 3413.0 * 263.0 / 53693175.0;

int ElapsedPs1Frames(bool reset) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (reset || g_clock.QuadPart == 0 || g_frequency.QuadPart == 0) {
    g_clock = now;
    g_pending = 0.0;
    return 1;
  }
  g_pending += static_cast<double>(now.QuadPart - g_clock.QuadPart) / g_frequency.QuadPart / kPs1FrameSeconds;
  g_clock = now;
  int n = static_cast<int>(g_pending);
  g_pending -= n;
  return n > 4 ? 4 : n;
}

void WaitPs1Frame() {
  LARGE_INTEGER start, now;
  QueryPerformanceCounter(&start);
  const LONGLONG ticks = static_cast<LONGLONG>(kPs1FrameSeconds * g_frequency.QuadPart);
  do {
    Sleep(1);
    QueryPerformanceCounter(&now);
  } while (now.QuadPart - start.QuadPart < ticks);
}

uint8_t* Game() { return *reinterpret_cast<uint8_t**>(game::kGameState); }

int MainState() {
  const uint8_t* g = Game();
  return g ? g[game::kMainState] : -1;
}

int PlayerHp() {
  const uint8_t* stats = *reinterpret_cast<uint8_t**>(game::kGameStats);
  return stats ? game::Read<int>(stats, game::kGameStatsHp) : 1;
}

void Send() {
  const int state = MainState();
  SurvivorScreenFx fx{};
  fx.size = sizeof fx;
  fx.fade = ps1::FadeDrawLevel(g_fade);
  if (g_dying && g_death.trail) {
    fx.trail = 2;  // 800A0CB4 = 3: 80085814
    fx.trailColor = g_death.trailColor;
    fx.trailScale = g_death.trailScale;
    fx.subtract = g_death.subtract;
  }
  fx.redFlash = g_dying && g_redFlash;
  fx.keep = g_config.ps1DoorFade && state == kStateRooms;
  fx.noCrosshair = g_dying;
  if (state == kStateContinue && g_gameOver >= 0) {
    // 8007B2A8: GAME OVER rises by 2 per frame from 800B6070 = -0x20 over the Continue picture, which stays. The
    // PC composes the screen only once: the last frame without the words is kept (keep until they are loaded)
    // and faded out over the one with the words.
    const int lit = (g_gameOver - ps1::kGameOverWaitFrames) * ps1::kGameOverFadeSpeed;
    fx.keep = !g_gameOverLoaded;
    fx.heldBlend = lit <= 0 ? 0xFF : (lit >= 0xFF ? 0 : 0xFF - lit);
  }
  SurvivorPc_SetScreenFx(&fx);
}

void EndDeathView() {
  SetDeathView(false, 0, 0, 0);
  SetDeathSlide(0);
}

// One frame of 8008369C.
void DeathTick() {
  const int pitchBefore = g_death.pitch;
  const ps1::DeathFrame f = ps1::StepPlayerDeath(g_death, g_fade);
  g_deathPitch += g_death.pitch - pitchBefore;
  ps1::RecoverPitch(g_deathPitch);  // 800785C0 -> 800789F8 in the same frame
  g_redFlash = f.redFlash;
  SetDeathSlide(f.slide);
  SetDeathView(true, g_death.eye - g_deathEyeStart, g_deathPitch, g_death.twist);
}

void OnStateChange(int from, int to) {
  if (g_config.ps1DoorFade && to == kStateRooms && from != kStateRooms) {
    // TEST 8006CAA0 at 8006CBD8: the loaded game starts with 80090B90(0,8).
    // This also resets any held black screen when a new/continued game enters its first room.
    ps1::FadeIn(g_fade, 8);
    Log("ScreenFx: room entry fade from black, PS1 step 8");
  }
  if (g_config.ps1PlayerDeath && to == kStateYouDied) {
    // 8008369C step 7: the black loading screen, then YOU DIED and 80090B90(0, 8); 800BB074 = 0.
    g_fade.state = 4;
    g_fade.level = 0xFF;
    g_youDiedLoad = ps1::kYouDiedLoadFrames;
    g_youDiedFrames = 0;
    g_youDiedLeaving = false;
    g_injectConfirm = false;
    g_confirmHeld = true;  // a button pressed after the screen opens is needed
    g_youDiedCredit = 0;
    g_youDiedClock = 0;
    g_youDiedLastPhase = -1;
  } else if (g_config.ps1PlayerDeath && from == kStateYouDied && to == kStateContinue) {
    // Step 0xB: AEX.DAT on a black screen (8007ABF0), then 80090B90(0, 8) with the Continue.
    g_fade.state = 4;
    g_fade.level = 0xFF;
    g_continueLoad = ps1::kContinueLoadFrames;
    g_continueClock = 0;
  }
  if (from == kStateContinue && g_gameOver >= 0) {
    // ASSUMPTION: the title comes out of black at speed 8 like the other screens.
    g_gameOver = -1;
    if (ps1::FadeBlack(g_fade)) ps1::FadeIn(g_fade, ps1::kYouDiedFadeSpeed);
  }
  if (from == kStateRooms && g_dying) {
    g_dying = false;
    EndDeathView();
  }
}

int __fastcall EndFrameDetour(void* game, void* edx, int a) {
  const int state = MainState();
  if (state != g_lastState) {
    OnStateChange(g_lastState, state);
    g_lastState = state;
  }
  const int frames = ElapsedPs1Frames(g_roomTicked);
  g_roomTicked = false;
  for (int i = 0; i < frames; ++i) {
    if (state == kStateYouDied && g_youDiedLoad > 0) {
      if (--g_youDiedLoad == 0) ps1::FadeIn(g_fade, ps1::kYouDiedFadeSpeed);
      continue;
    }
    if (state == kStateContinue && g_continueLoad > 0) {
      if (--g_continueLoad == 0) ps1::FadeIn(g_fade, ps1::kYouDiedFadeSpeed);
      continue;
    }
    if (state == kStateContinue && g_gameOver >= 0) {
      ++g_gameOver;
      if (g_gameOver == ps1::kGameOverEndFrames) ps1::FadeOut(g_fade, ps1::kYouDiedFadeSpeed);
      if (g_gameOver > ps1::kGameOverEndFrames && ps1::FadeBlack(g_fade)) g_gameOverDone = true;
    }
    if (state == kStateYouDied) {
      ++g_youDiedCredit;
      ++g_youDiedClock;
      // Steps 8-9: 800BB074 + 1 up to 0x28; after the fade to black (80090BF0(0, 8)) the Continue.
      if (g_youDiedFrames < ps1::kYouDiedMinFrames) ++g_youDiedFrames;
      if (g_youDiedLeaving && ps1::FadeBlack(g_fade)) g_injectConfirm = true;
    }
    ps1::StepFade(g_fade);
  }
  Send();
  return g_endFrame(game, edx, a);
}

uint32_t __fastcall ReadPadDetour(void* game, void* edx, int a) {
  uint32_t pad = g_readPad(game, edx, a);
  if (g_dying) return 0;  // 8008369C step 0: 80062DEC = 0, controls off
  if (g_config.ps1PlayerDeath && MainState() == kStateYouDied) {
    // Step 9: a button just pressed (800B4B94 & 0x20 and not 800B3CC4) after at least 0x28 frames.
    const bool down = (pad & game::kPadConfirm) != 0;
    const bool press = down && !g_confirmHeld;
    g_confirmHeld = down;
    pad &= ~game::kPadConfirm;
    if (g_injectConfirm) {
      g_injectConfirm = false;
      g_youDiedLeaving = false;
      return pad | game::kPadConfirm;  // 0046A050: YOU DIED -> Continue
    }
    if (press && !g_youDiedLeaving && g_youDiedLoad == 0 && g_youDiedFrames >= ps1::kYouDiedMinFrames) {
      g_youDiedLeaving = true;
      ps1::FadeOut(g_fade, ps1::kYouDiedFadeSpeed);
    }
  }
  return pad;
}

int __fastcall MouseClickDetour(void* mouse, void* edx, int a, int b, int c, int d, int e, int f) {
  if (g_dying) return 0;  // no shooting during the death
  return g_mouseClick(mouse, edx, a, b, c, d, e, f);
}

int __fastcall RoomTickDetour(void* game, void* edx, uint32_t* pad, uint32_t* prev, int* turns, float p5, int* scene,
                              int p7, float p8) {
  g_roomTicked = true;
  const int result = g_roomTick(game, edx, pad, prev, turns, p5, scene, p7, p8);
  if (!g_config.ps1PlayerDeath) return result;
  if (g_dying) {
    DeathTick();
    if (!ps1::PlayerDeathDone(g_death)) return 1;
    // Step 6: 800A55A2 = 4, the trail goes off and the room leaves for YOU DIED (scene 0, +351C = 7).
    g_dying = false;
    EndDeathView();
    if (scene) *scene = 0;
    Log("Ps1PlayerDeath: PS1 sequence over, the room exits");
    return 0;
  }
  if (result == 0 && scene && *scene == 0 && PlayerHp() <= 0) {
    // 8008480C: with health at 0, player state 3 (8008369C) instead of the immediate PC exit.
    // The eyes start from the frame's height, bob included (DuckStation 2026-10-23: -1642 -> -122 in 19
    // frames of 0x50, 19 pitch steps).
    g_deathEyeStart = ps1::kEyeRest + PlayerEyeBob();
    ps1::StartPlayerDeath(g_death, g_deathEyeStart);
    g_deathPitch = 0;
    g_dying = true;
    DeathTick();
    Log("Ps1PlayerDeath: health at 0, PS1 sequence (fall, red flash, zoom, black)");
    return 1;
  }
  return result;
}

void __fastcall DrawHudDetour(void* hud, void* edx) {
  // ASSUMPTION: on the PS1 the HUD disappears with step 0 of the death (80062DEC = 0); DuckStation frames of
  // 2026-10-22 (death at 5.7 s: HUD present, at 6.1 s gone).
  if (g_dying) return;
  g_drawHud(hud, edx);
}

void __fastcall DoorAnimDetour(void* door, void* edx, const char* path, int kind) {
  if (g_config.ps1DoorFade) {
    // 8009016C steps 1-2: 80090BF0(0, 0x10) over the room; the PC room has already freed its objects,
    // so its last frame is drawn again (kept with keep).
    ps1::FadeOut(g_fade, 0x10);
    int frames = 0;
    while (!ps1::FadeBlack(g_fade) && frames < 64) {
      ps1::StepFade(g_fade);
      Send();
      if (!SurvivorPc_PresentHeldFrame()) break;
      WaitPs1Frame();
      ++frames;
    }
    if (!ps1::FadeBlack(g_fade)) {
      ps1::FadeOut(g_fade, 0xFF);
      ps1::StepFade(g_fade);
    }
    // Step 5: 80090B90(0, 0x10) with the door animation.
    ps1::FadeIn(g_fade, 0x10);
    Send();
  }
  g_doorAnim(door, edx, path, kind);
  if (g_config.ps1DoorFade) {
    // Step 0xD: the new room out of black, 80090B90(0, 0x80) (0x10 only for one case of 800A866C = 0x10).
    ps1::FadeIn(g_fade, 0x80);
    Send();
  }
}

// PC YOU DIED (00591900): one step every 26 ms (timeGetTime - [0] >= 0x1A). Phase [1] 0: the zoom, [2] from
// 0x28 to 0x3C, vertex step [2] * width * [5AE684] * [5AE680]; 1: the reveal of the low rows, [2] from 0x28
// to 0x12, v offset [2] * [5AE668]; 2: the wave, front [2] + 8 up to 0x80 and again. Here one step per PS1
// frame of 8007A874, with the counter held and the factor giving the wanted value, so nothing stutters:
// zoom like camera 800B3EF0 (from -4000 at +15 per frame: 0x28 * 4000 / (4000 - 15 f), until it reaches
// 0x3C), reveal from 0x28 to 0x12 in the 0x40 frames of 800B6060, wave front + 1 per frame (800B605C).
// ASSUMPTION: the PC scale (vertex step) matches the PS1 camera's.
float* const kZoomFactor = reinterpret_cast<float*>(0x005AE684);    // 1/60, only in 00591900
float* const kRevealFactor = reinterpret_cast<float*>(0x005AE668);  // only in 00591900
bool g_factorsWritable = false;

int __fastcall YouDiedDetour(DWORD* screen, void* edx, int a) {
  if (!g_config.ps1PlayerDeath || !screen || screen[5] == 0 || !g_factorsWritable) return g_youDied(screen, edx, a);
  const DWORD now = timeGetTime();
  const int phase = static_cast<int>(screen[1]);
  if (phase != g_youDiedLastPhase) {
    g_youDiedLastPhase = phase;
    g_youDiedPhaseStart = g_youDiedClock;
  }
  if (g_youDiedLoad > 0 || g_youDiedCredit <= 0) {
    screen[0] = now;  // no step: held during the black screen or between two PS1 frames
    return g_youDied(screen, edx, a);
  }
  g_youDiedCredit = 0;
  screen[0] = now - 0x1A;
  const int f = g_youDiedClock - g_youDiedPhaseStart;
  const float zoom = *kZoomFactor, reveal = *kRevealFactor;
  if (phase == 0) {
    // Camera from -4000 to -2501: the size goes from 2501/4000 to 1 of the final one.
    if (f < ps1::kYouDiedZoomFrames) {
      const float want = ps1::kYouDiedFinalCounter * 2501.0f / (4000.0f - 15.0f * static_cast<float>(f));
      screen[2] = 39;  // becomes 0x28 in the step
      *kZoomFactor = zoom * want / 40.0f;
    } else {
      screen[2] = 0x3C;  // the step brings it to 0x3D: phase 1, the vertices stay where they were
    }
  } else if (phase == 1) {
    const float want = 40.0f - 22.0f * static_cast<float>(f) / ps1::kYouDiedRevealFrames;
    if (want > 18.0f) {
      screen[2] = 41;  // becomes 0x28 in the step
      *kRevealFactor = reveal * want / 40.0f;
    } else {
      screen[2] = 0x12;  // the step brings it to 0x11: phase 2
    }
  } else if (phase == 2) {
    screen[2] = static_cast<DWORD>(f % 0x78);  // + 8 in the step; the PC starts again at 0x80
  }
  const int result = g_youDied(screen, edx, a);
  *kZoomFactor = zoom;
  *kRevealFactor = reveal;
  return result;
}

// PC Continue (00592520): the digit changes when timeGetTime - [0] > 1000 and the light follows (time / 200) % 5.
// On the PS1 (8007B2A8) the digit lasts 0x3C frames (2 s) and the light goes up and down in the same period: here
// the time the screen sees runs at half speed, and stays still during the black loading screen.
int __fastcall ContinueDetour(DWORD* screen, void* edx, int a, char continues) {
  if (g_config.ps1PlayerDeath && screen && screen[1] == 2) {
    // GAME OVER: the PC shows it at once at full light for 49 steps of 100 ms. Here black, wait, out of black,
    // held until -0x14C, to black and only then the step that closes the screen (result 0).
    const DWORD now = timeGetTime();
    if (g_gameOver < 0) {
      g_gameOver = 0;
      g_gameOverDone = false;
      g_gameOverLoaded = false;
    } else if (g_gameOverDone) {
      screen[2] = 0x31;
      screen[0] = now - 101;
    } else if (screen[2] >= 1) {
      screen[0] = now;  // GAME OVER loaded (first step): held
    }
    const int result = g_continue(screen, edx, a, continues);
    if (screen[2] >= 1) g_gameOverLoaded = true;
    return result;
  }
  if (g_config.ps1PlayerDeath && screen && screen[1] == 0 && screen[5] != 0) {
    const DWORD now = timeGetTime();
    if (g_continueClock != 0) {
      const DWORD delta = now - g_continueClock;
      screen[0] += g_continueLoad > 0 ? delta : delta / 2;
    }
    g_continueClock = now;
  }
  return g_continue(screen, edx, a, continues);
}

}  // namespace

bool PlayerDying() { return g_dying; }

bool InstallScreenFx() {
  QueryPerformanceFrequency(&g_frequency);
  const game::ByteSignature endFrame = {game::kEndFrame, game::kEndFrameBytes, sizeof game::kEndFrameBytes};
  const game::ByteSignature readPad = {game::kReadPad, game::kReadPadBytes, sizeof game::kReadPadBytes};
  const game::ByteSignature click = {game::kMouseClick, game::kMouseClickBytes, sizeof game::kMouseClickBytes};
  const game::ByteSignature tick = {game::kRoomTick, game::kRoomTickBytes, sizeof game::kRoomTickBytes};
  const game::ByteSignature hud = {game::kDrawHud, game::kDrawHudBytes, sizeof game::kDrawHudBytes};
  const game::ByteSignature door = {game::kDoorAnim, game::kDoorAnimBytes, sizeof game::kDoorAnimBytes};
  const game::ByteSignature youDied = {game::kYouDiedScreen, game::kYouDiedScreenBytes,
                                       sizeof game::kYouDiedScreenBytes};
  const game::ByteSignature cont = {game::kContinueScreen, game::kContinueScreenBytes,
                                    sizeof game::kContinueScreenBytes};
  if (!game::CheckBytes(endFrame) || !game::CheckBytes(readPad) || !game::CheckBytes(click) ||
      !game::CheckBytes(tick) || !game::CheckBytes(hud) || !game::CheckBytes(door)) {
    Log("ScreenFx: unexpected bytes (00466CD0, 00466E10, 00407810, 0046A520, 0042C3D0 or 0058D560), feature off");
    g_config.ps1PlayerDeath = false;
    g_config.ps1DoorFade = false;
    return false;
  }
  bool ok = Hook("EndFrame", game::kEndFrame, reinterpret_cast<void*>(&EndFrameDetour),
                 reinterpret_cast<void**>(&g_endFrame));
  if (ok && g_config.ps1PlayerDeath) {
    ok = Hook("ReadPad", game::kReadPad, reinterpret_cast<void*>(&ReadPadDetour),
              reinterpret_cast<void**>(&g_readPad)) &&
         Hook("MouseClick", game::kMouseClick, reinterpret_cast<void*>(&MouseClickDetour),
              reinterpret_cast<void**>(&g_mouseClick)) &&
         Hook("RoomTick", game::kRoomTick, reinterpret_cast<void*>(&RoomTickDetour),
              reinterpret_cast<void**>(&g_roomTick)) &&
         Hook("DrawHud", game::kDrawHud, reinterpret_cast<void*>(&DrawHudDetour),
              reinterpret_cast<void**>(&g_drawHud));
    DWORD old = 0;
    g_factorsWritable = VirtualProtect(reinterpret_cast<void*>(0x005AE668), 0x20, PAGE_READWRITE, &old) != FALSE &&
                        *kZoomFactor > 0.0166f && *kZoomFactor < 0.0167f;
    if (ok && game::CheckBytes(youDied) && game::CheckBytes(cont)) {
      ok = Hook("YouDiedScreen", game::kYouDiedScreen, reinterpret_cast<void*>(&YouDiedDetour),
                reinterpret_cast<void**>(&g_youDied)) &&
           Hook("ContinueScreen", game::kContinueScreen, reinterpret_cast<void*>(&ContinueDetour),
                reinterpret_cast<void**>(&g_continue));
    } else if (ok) {
      Log("ScreenFx: unexpected bytes at 00591900 or 00592520, YOU DIED and Continue keep the PC timing");
    }
  } else if (ok) {
    ok = Hook("RoomTick", game::kRoomTick, reinterpret_cast<void*>(&RoomTickDetour),
              reinterpret_cast<void**>(&g_roomTick));
  }
  if (ok && g_config.ps1DoorFade) {
    ok = Hook("DoorAnim", game::kDoorAnim, reinterpret_cast<void*>(&DoorAnimDetour),
              reinterpret_cast<void**>(&g_doorAnim));
  }
  return ok;
}

}  // namespace sf::features
