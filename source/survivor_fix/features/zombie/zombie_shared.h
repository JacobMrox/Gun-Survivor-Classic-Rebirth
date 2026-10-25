// Zombies: state and functions shared by the PS1 fixes.
// One file per fix: zombie_fall (forward/backward fall), zombie_bite (bite, grab from behind,
// lunge, bite sequence), zombie_hit (reactions to hits), zombie_death (death), zombie_pose
// (head and torso pose), zombie_blood (blood on hit), zombie_push (push and separation),
// zombie_nav (chase), zombie_walk (walk and speed), zombie_world_pc (shared PS1 world),
// zombie_common (common state, mapper and update detours, installation).
#pragma once

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "../../config.h"
#include "../../game.h"
#include "../../hooks.h"
#include "../../log.h"
#include "../../ps1/backfall_logic.h"
#include "../../ps1/bite_logic.h"
#include "../../ps1/blend_logic.h"
#include "../../ps1/damage_view_logic.h"
#include "../../ps1/hit_logic.h"
#include "../../ps1/lunge_logic.h"
#include "../../ps1/speed_logic.h"
#include "../../ps1/stumble_logic.h"
#include "../../ps1/zombie_profile.h"
#include "../../ps1/zombie_world.h"
#include "../features.h"

namespace sf::features::zombie {

using MapAnimFn = int(__fastcall*)(void* s, void* edx, int request);
using UpdateEnemyFn = int(__fastcall*)(void* s, void* edx);
using AccumulateFn = void(__fastcall*)(void* o, void* edx, float x, float y, float z);
extern MapAnimFn g_mapAnim;
extern UpdateEnemyFn g_updateEnemy;
// Not hooked: only called (ABI in game.h).
inline const auto g_accumulate = reinterpret_cast<AccumulateFn>(game::kAccumulate);
// __thiscall(S) emulated with __fastcall.
using HitPlayerFn = void(__fastcall*)(void* s, void* edx);
inline const auto g_hitPlayer = reinterpret_cast<HitPlayerFn>(game::kHitPlayer);
using TurnTowardFn = void(__fastcall*)(void* o, void* edx, float x, float y, float z);
inline const auto g_turnToward = reinterpret_cast<TurnTowardFn>(game::kTurnToward);
// 00436B40 RequestAnim(S, request, 1), __thiscall (0043B433: push 1; push request).
using RequestAnimCallFn = int(__fastcall*)(void* s, void* edx, int request, int flag);
inline const auto g_requestAnimCall = reinterpret_cast<RequestAnimCallFn>(game::kRequestAnim);

// Family membership is centralised in ps1::IsPcZombie.
// Past this number of updates in a phase the zombie goes back to the enemy logic (S reused,
// sequence never applied, cursor never reached).
constexpr int kPhaseTimeout = 900;
// A zombie counts as alive if its update ran recently: entries of previous rooms stay in
// the map but their S must not be read any more.
constexpr DWORD kAliveMs = 500;

enum class Phase {
  Idle, Fall, GetUp1, GetUp2, BackFall, Supine, SitUp,
  // PS1 lunge (state 0xB, 80103868) and the reaction after the bite (0x20, state 6).
  Lunge, LungeMiss, LungeGrab, LungeBite, Knockback,
  // PS1 death (state 7, 801032CC): clip chosen by 80101700, last pose held.
  Death,
  // Dead player (80102828 zone 4 with the player in state 3): E+316 = 4, 800802C8(E, 0, 0x17), the zombie
  // bends over the body like the one eating in the alley (state 0, 80102468) and stays there (80104534 does
  // nothing before 300 frames).
  Feed
};
constexpr int kFeedClip = 0x17;

struct Entry {
  ps1::StumbleState state;
  int prevRequest = -1;
  uint32_t walkAudioTick = UINT32_MAX;
  Phase phase = Phase::Idle;
  int updates = 0;
  bool applied = false;
  bool backRolled = false;  // roll of 0x20 already done for this application
  ps1::BackPush push;
  ps1::NavState nav;
  ps1::StumbleState lungeBand;  // band of 80102828 for the lunge
  ps1::LungeBits lungeBits;     // E+35C bits 2/4
  ps1::GrabSlide slide;         // 80103868 substates 3/4
  bool lungeBitten = false;     // bite of 0x1E already given
  int rememberedWalk = 0;       // preserves variant-specific native/preset locomotion
  int forceWalk = 0;            // walk to impose (800802C8(E,4,anim)); 0 = none
  int q1Cursor = -1;            // last cursor of track 1 (PS1 end of clip)
  int q1Stall = 0;
  bool q1Advanced = false;
  // PS1 blend for the next walk chosen by the enemy logic (80104108 cases 5/7 at the end of 0F/0x11:
  // 80080268(E, 80101CE4(E,2)) with E+8B0 = 0x180); -1 = none.
  int nextBlend = -1;
  // PS1 blend of this phase when it depends on how it was entered (-1 = Ps1Blend(phase)).
  int phaseBlend = -1;
  // Updates still held for the PS1 blend of the clip just applied (80104108, LAB_801042A8):
  // the PS1 world cancels the planned movement, the contacts stay.
  int stillUpdates = 0;
  // Knockback as a reaction to a hit (state 6, 80102D74): clip (0 = 0x20), push direction
  // (+1 hit from the front: backwards) and P+0C / P+10 (hit_logic.h).
  int reactClip = 0;
  int deathClip = 0;  // clip of the running PS1 death (Phase::Death)
  // P+0C in the chase (80102828): 0 = to roll below 1500, 1 = arms forward (clip 4), 2 = no; after a
  // reaction from the table it holds the push (80101474: 1000/500/200) and the roll is not done again
  // until a hit without reaction sets it back to 0.
  int armsRoll = 0;
  // Stumble: outcome decided at start+8 (80103C6C substate 1), 0x10 starts 2 frames later (0x23 reaches
  // key 9: DuckStation 2026-10-22, 0x23 for 18 frames). -1 = no outcome pending.
  int fallExitAt = -1;
  bool fallExitContact = false;
  // Direction held in the phases in which the PS1 does not turn the zombie (HoldHeading).
  bool headingHeld = false;
  float heldFx = 0.0f, heldFz = 1.0f;
  float pushSign = 1.0f;
  int slideBase = 0;
  int slideP10 = 500;
};

extern std::unordered_map<void*, Entry> g_entries;
extern uint32_t g_rng;

struct Geometry {
  float distance;  // planar, like 8007F4F4
  float toX, toZ;  // from O to T
};

// Attack request (mapper 00436EE6) and PS1 bite clip (80102C38).
constexpr int kAttackRequest = 0x08;
constexpr int kAttackRequestMap = 0x08;
constexpr int kBiteClip = 0x1B;
constexpr int kBiteClipAfter2F = 0x24;
// ASSUMPTION: the player's PC y is 1600 above the feet (spawn: player -1600, zombie 0 on
// the same floor); it serves the +/-800 height checks of 80081FF0.
constexpr float kPlayerEyeHeight = 1600.0f;

struct BiteGate {
  uint8_t* o = nullptr;
  float savedScale = 0.0f;
  bool blocked = false;
  bool wasAttacking = false;
  bool freeze = false;  // grab from behind: the zombie does not move in this update
  bool ready = false;   // PS1 counter ready: 80102828 goes to state 5 (bite)
};

// Shared world bridge; no extra hook ABI.
bool WorldEnabled();
bool WorldOwnsObject(const uint8_t* o);
bool WorldBeginUpdate(void* s, uintptr_t caller);
void WorldPlanWalk(void* s, float fx, float fz, bool walk);
void WorldFinishUpdate(void* s);
bool WorldMove(uint8_t* o, float dx, float dz);
void SyncObjectPose(uint8_t* o);
void ResetBiteActor(void* s);
void ResetBites();
void ResetPushActor(void* s);
void ResetPushes();
void ResetWalkRoom();

// zombie_common.cpp
int NextRngByte();
bool IsLocomotion(int request);
bool IsDeath(int request);
bool IsBackChain(Phase phase);
bool IsLungePhase(Phase phase);
float PhaseSpeed(Phase phase);
int PhaseSelector(Phase phase);
bool GetObjects(void* s, uint8_t*& o, uint8_t*& t);
Geometry Measure(const uint8_t* o, const uint8_t* t);
bool InSectorOf(const uint8_t* viewer, float toX, float toZ);
float AngleDegrees(const uint8_t* viewer, float toX, float toZ);
uint8_t* ExitTrack(const uint8_t* o);
int TrackStartCursor(const uint8_t* q);
void SetPhase(void* s, Entry& e, Phase phase, const char* why);
bool Q1Completed(const uint8_t* o, Entry& e);
void HoldSequence(void* s, Entry& e, int& result);
// PS1 blend (E+8B0 units) for the next sequence application of this zombie.
void RequestPs1Blend(void* s, int ps1Units);
int Ps1Blend(Phase phase);
int ApplyPendingBlend(void* s, int result);
void SetHeading(uint8_t* o, float fx, float fz);
void MoveZombie(uint8_t* o, float dx, float dz);
ps1::Vec2 PlayerForward(const uint8_t* t);
float AngleOf(ps1::Vec2 v);
int AngleUnits(float radians);
void HoldPlayer(uint8_t* t, float x, float z);

// zombie_fall.cpp
void Step(void* s, Entry& e);
bool TryStartBackFall(void* s, Entry& e, int& result);
bool RollBackFall(void* s, Entry& e);
int MapAnimInner(void* s, void* edx, int request);
void UpdateFallPhase(void* s, Entry& e);

// zombie_bite.cpp
bool IsBiting(void* s);
bool GrabActiveFor(void* s);
bool PlayerHeld();
bool TryStartLunge(void* s, Entry& e, int request, int& result);
int ApplyBiteClip(void* s, int request, int result);
BiteGate BiteBefore(void* s);
void BiteAfter(void* s, const BiteGate& g);
void UpdateBiteSequence(void* s);
void UpdateLunge(void* s, Entry& e);
void FinishKnockback(void* s, Entry& e);
bool ExitTrackWrapped(void* s, const Entry& e);
bool BoneWorld(const uint8_t* o, unsigned index, float w[3]);

// zombie_hit.cpp
struct HitGate {
  bool active = false;  // the update takes the PC branch without reaction
  ps1::HitKind kind = ps1::HitKind::None;
  int savedRequest = 0;
  int selector = -1;
  int clip = -1;
  int rng = -1;
  bool behind = false;
  float distance = 0.0f;
  // Track cursors (Q+40, Q+44) before the update: the PC branch without reaction moves them back
  // by 5 (0043A8A0, the PC "flinch") and they are restored, like on the PS1, which does not do it.
  static constexpr int kMaxTracks = 8;
  int tracks = 0;
  int q40[kMaxTracks] = {};
  int q44[kMaxTracks] = {};
};
int SelectorOf(const Entry& e);
int HitMapRequest(void* s, int request);
HitGate HitBefore(void* s);
void HitAfter(void* s, const HitGate& g);
// Update with the PC branch without reaction in progress (the cursors are 5 back until HitAfter).
bool HitRewindActive();

// zombie_death.cpp
bool DeathMap(void* s, Entry& e, int request, int& result);
void UpdateDeath(void* s, Entry& e);
bool DeathLying(void* s);
void DeathAfterDraw(void* s);
void ResetDeathActor(void* s);
void ResetDeaths();
bool InstallZombieDeath();

// zombie_blood.cpp
void SetBloodSubject(void* s);
bool InstallZombieBlood();

// zombie_pose.cpp
void PoseOnHit(void* s, bool behind);
void UpdatePose(void* s, const Entry& e);
bool InstallZombiePose();

// zombie_push.cpp
void PushPlayer(void* s);
void SeparateZombie(void* s);

// zombie_nav.cpp
int UpdateWithNavigation(void* s, void* edx);

// zombie_audio.cpp
bool InstallZombieWalkAudio();
void WalkAudioBefore(void* s);
void WalkAudioAfter(void* s);

// zombie_walk.cpp
int ApplyWalkHold(void* s, int request, int result);
void ScaleWalk(void* s);
int ZombieWalkSpeed(void* s, float distance);

}  // namespace sf::features::zombie
