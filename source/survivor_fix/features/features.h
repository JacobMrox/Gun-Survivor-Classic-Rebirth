#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

#include "../ps1/nav_logic.h"
#include "../ps1/wall_logic.h"
#include "../ps1/zombie_voice_logic.h"

namespace sf::features {

// Every Install*() checks the expected bytes, hooks and returns true if active.

// Lifecycle notifications used by the verified RequestAnim initializer call.
namespace zombie {
struct QuickState {
  int32_t slot = -1;
  int32_t type = -1;
  float health = 0.0f;
  uint8_t active = 0;
  uint8_t reserved[3] = {};
  float position[3] = {};
  float matrix[16] = {};
};
void BeginWalkSpeedLife(void* s);
void NativeZombieLife(void* s);
void ObserveZombieVoiceLife(void* s);
void ResetZombieRoom();
size_t CaptureQuickStates(QuickState* states, size_t capacity);
void QueueQuickStateRestore(const QuickState* states, size_t count);
void WorldPhysicsBegin(void* c);
void WorldPhysicsEnd(void* c);
void WorldBeforeQuery(void* c, int index);
void WorldBridgeReady(bool ready);
}  // namespace zombie

// Start of a zombie's life (zombie_life.cpp): the native initialisation of S (4361B0 -> 436B04).
bool InstallZombieLife();

// Bite from behind too (removes the "front only" filter of 0043A8A0).
bool InstallBiteFromBehind();

// InputDeviceFix: unknown DirectInput device types no longer hang or crash the start-up (game.h,
// kInputDeviceCount). From DllMain, before the game's code runs.
void ApplyInputDeviceFix();
// Native CONDITION FLC array bound; keeps the native timing and D3D.
bool InstallHudConditionBounds();
bool InstallMovieSkip();
// F5/F6 game-aware quick save/load and F8 window/fullscreen toggle.
bool InstallQuickSave();
int QuickSaveAfterRoomTick(void* gameObject, int nativeResult);
void QuickSavePollWindowToggle();
bool QuickSaveHandleWindowMessage(unsigned message, uintptr_t wparam, intptr_t lparam);
// Returns the current user-facing hotkey/result notice and its fade alpha.
// Routine diagnostics stay in hook.log; this channel is only for actions and actionable failures.
bool QuickSaveGetNotification(char* text, size_t capacity, float* alpha);
bool QuickSaveReloadPending();
bool QuickSaveReloadFadeStarted();
void QuickSaveStartReloadFade();
bool QuickSaveApplyPendingReload(void* gameObject, int* scene);
bool QuickSaveConsumeRoomEntry();
bool InstallTitleFade();
bool InstallMouseInput();
bool InstallPs1Cadence();
bool ApplyItemGlitterCadence();
// One stamp per native gameplay loop; not a render frame or per-actor update.
bool Ps1LogicFrame(uint32_t* tick);
bool InstallPs1ZombieShadowSize();
// ExitCrashFix (exit_fix.cpp): no crash at 00466D61 when the game closes in the middle of a frame (0046FEC0).
bool InstallExitCrashFix();

// Size of the final picture: ForceWidth x ForceHeight of config.ini.
bool DisplaySize(int* w, int* h);

// Current room (00475530) and its PS1 walls and doors from ROOMENV.DAT (room_walls.cpp).
bool InstallRoomWalls();
const std::vector<ps1::Wall>& CurrentRoomWalls();
const std::vector<ps1::DoorEvent>& CurrentRoomDoors();
const std::vector<ps1::Wall>& CurrentEnemyOnlyWalls();
bool IsCurrentRoom(int stage, int room);
bool CurrentRoomGeometryReady();
const std::vector<ps1::VoiceProfile>& CurrentRoomVoiceProfiles();

// Player (player.cpp): hooks 004609D0 (physics) and 004603F0 (rotation) for the PS1 movement, walls and view.
bool InstallPlayer();
// Player object state that is not part of the game's normal 77-byte save
// record.  Restoring is deferred until the destination room has constructed
// its player object, otherwise the native entrance spawn overwrites it.
struct PlayerQuickState {
  float position[3] = {};
  float matrix[16] = {};
};
bool CapturePlayerQuickState(PlayerQuickState* state);
void QueuePlayerQuickStateRestore(const PlayerQuickState& state);
// PS1 view jolt on the bite (800830F8): direction offset (800A865E, 4096 per turn) and y of the look-at
// point (800B3EF8, 500 at rest). The bite sequence sets it at every update.
void SetDamageViewJolt(int yawUnits, int lookY);
// PS1 death (screen_fx.cpp, 8008369C): eyes `eye` lower (PS1 units), pitch `pitch` (4096 per turn,
// negative = up) and twist `twist` (4096 = 1 degree) of the player's view.
void SetDeathView(bool active, int eye, int pitch, int twist);
// At the next physics frame the player slides by (0, 0, slide) along their PS1 axes (positive = forward).
void SetDeathSlide(int slide);
// PS1 bob of the eyes in this frame (800A8634 + 0x640), 0 without Ps1HeadBob.
int PlayerEyeBob();
// True only in the outer approach band of an original scripted interaction,
// never while the player is already inside its native activation radius.
bool ItemApproachPromptVisible();
// PS1 screen effects (screen_fx.cpp): Ps1PlayerDeath, Ps1DoorFade.
bool InstallScreenFx();
// The player is in PS1 state 3 (8008369C, Ps1PlayerDeath): no jolt or view rotation.
bool PlayerDying();

// PS1 zombies (zombie/): stumble, bite, hits, death, pose, blood, push, chase, walk.
bool InstallZombies();

}  // namespace sf::features
