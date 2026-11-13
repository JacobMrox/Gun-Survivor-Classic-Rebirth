#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

#include "../ps1/nav_logic.h"
#include "../ps1/wall_logic.h"
#include "../ps1/zombie_voice_logic.h"
#include "../xinput_logic.h"

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

// Mr. X (tyrant.cpp): PC types 22/23 get the PS1 walk/idle choice from the travel the native physics
// produced, and the PS1 death clip 9 (ps1/tyrant_logic.h). No movement, collision or attack is replaced.
namespace tyrant {
void BeginUpdate(void* s, bool active);
void Finish(void* s);
void Map(void* s, int request, int& result);
void OnPlayerShot();
void Reset();
}  // namespace tyrant

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
void ScreenFxOnPlayerShot();
int QuickSaveAfterRoomTick(void* gameObject, int nativeResult);
void QuickSavePollWindowToggle();
bool QuickSaveHandleWindowMessage(unsigned message, uintptr_t wparam, intptr_t lparam);
// Returns the current user-facing hotkey/result notice and its fade alpha.
// Routine diagnostics stay in hook.log; this channel is only for actions and actionable failures.
bool QuickSaveGetNotification(char* text, size_t capacity, float* alpha);
bool QuickSaveReloadPending();
bool DebugMenuVisible();
bool DebugMenuUtilityVisible();
bool DebugUtilityCheatAvailable(int row);
bool DebugUtilityCheatEnabled(int row);
bool DebugMenuSuppressPad(uint32_t pad);
// Pad menu input from the DirectInput hook (sf::pad::MenuNavMask bits); a fresh press moves,
// confirms or goes back while the menu is open.
void DebugMenuPadNav(uint32_t nav);
// Debug spawn (player.cpp): the actor appears kDebugSpawnDistance in front of the player through the
// native room-table spawn 00434850; the item duplicates a pickup the current room already registered,
// so its id and orientation are known valid here.
bool DebugSpawnEntity(int type);
bool DebugSpawnItemInFront();
// The single spawn row of the utility menu: which entry LEFT/RIGHT selected and placing it.
int DebugSpawnChoice();
void DebugSpawnChoiceCycle(int delta);
bool DebugSpawnChosen();
// The player's world position and horizontal forward, or false outside gameplay.
bool CurrentPlayerPose(float position[3], float forward[2]);
// Debug menu quit actions (quick_save.cpp): WM_CLOSE is the game's own shutdown, and the title
// entry leaves the room and lets the native boot state open the title.
void DebugQuitGame();
void DebugReturnToTitle();
// True once for the frame in which the room must exit for the title (false after 3 s).
bool DebugReturnToTitleRequested();
int DebugMenuSelected();
int DebugMenuRow();
int DebugMenuCurrentScene();
void DebugQueueJump(int scene);
// Pad bindings for the wrapper's own additions: the gamepad layer calls these on a fresh
// press of a button that is bound to one of these actions instead of a key.
void ToggleDebugMenu();
void QuickSaveHotkey(bool save);
void ToggleFullscreenWindow();
bool QuickSaveReloadFadeStarted();
void QuickSaveStartReloadFade();
bool QuickSaveApplyPendingReload(void* gameObject, int* scene);
bool QuickSaveConsumeRoomEntry();
// Completes a native Load Game selection with the matching extended slot state.
void QuickSaveOnNativeLoad(int slot, bool succeeded);
bool InstallTitleFade();
bool InstallMouseInput();
bool InstallPs1Cadence();
bool ApplyItemGlitterCadence();
void RegisterItemGlitterEffect(void* effect);
bool ItemGlitterShouldDraw(void* effect);
bool IsItemGlitterEffect(void* effect);
void ForgetItemGlitterEffect(void* effect);
void ResetItemGlitterEffects();
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
// Physical stage/room of the loaded room (false until the first room path).
bool CurrentRoomId(int* stage, int* room);
bool CurrentRoomGeometryReady();
const std::vector<ps1::VoiceProfile>& CurrentRoomVoiceProfiles();
const std::vector<ps1::Placement>& CurrentRoomPlacements();

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

// DualShock 4 (ds4.cpp): the lightbar breathes with the player's health and the controller speaker
// plays the gunshots. Ds4Tick runs from the render tick, Ds4OnPlayerShot from the native shot path.
bool InstallDs4();
void Ds4Tick();
void Ds4OnPlayerShot();
// A player hit landed (00463E70): times the hit rumble from the event, not from a health drop, so
// INFINITE HEALTH still rumbles while INVINCIBLE, which never reaches the native call, does not.
void Ds4OnPlayerDamage();
// The title's white flash (New Game and the confirmed Load Game slot) drives one rumble beat.
void Ds4OnScreenFlash();
// The newest DualShock 4 input report, for the pad overlay in dinputhook.cpp. The DS4 is a
// DirectInput pad, so the game never sees it without this.
bool Ds4PadState(sf::pad::PadState* out);
// The player is in PS1 state 3 (8008369C, Ps1PlayerDeath): no jolt or view rotation.
bool PlayerDying();
// The utility menu's INVINCIBLE switch: no player damage lands at all, so nothing shakes and no
// rumble is timed.
bool PlayerInvincible();

// PS1 zombies (zombie/): stumble, bite, hits, death, pose, blood, push, chase, walk.
// Mr. X (tyrant.cpp) rides the same two hooks.
bool InstallZombies();

}  // namespace sf::features
