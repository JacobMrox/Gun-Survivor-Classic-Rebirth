// Mr. X (PC types 22/23): the PS1 walk step (port/decomp/tyrant.c, n_tyrant_WalkStep 0x80101404).
//
// The PC enemy logic is not allowed to move him: the tyrant is a PS1 world actor (zombie_world_pc.cpp),
// so its XZ is planned here - heading from the shared PS1 nav (ps1/nav_logic.h, NavStep, the same one the
// zombies use) and one step of 26 units per update (52 while charging) - committed by
// WorldFinishUpdate -> StepZombieWorld (ROOMENV and enemy-only walls, actor and player contacts) and
// protected from the native physics by WorldPhysicsBegin/WorldPhysicsEnd, which restore the owned XZ and
// clear O+C8/O+D4. That is what removes the drift the PC logic added by itself (sideways, backwards, or
// an extra step on top of the walk).
//
// The clips come from the PC mapper's own case, which already uses the PS1 numbers: 1 walk, 0x11 charge,
// 0x13 aim stop, 5/0x15 sidestep, 9 death. The native update still resolves the attacks, the hit event,
// damage and death; everything it does with the body's direction and speed is replaced.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "../config.h"
#include "../game.h"
#include "../ps1/nav_logic.h"
#include "../ps1/tyrant_logic.h"
#include "features.h"
#include "zombie/zombie_shared.h"

namespace sf::features::tyrant {
namespace {

struct Track {
  const uint8_t* o = nullptr;
  int index = -1;
  // Position the tyrant was left at (absolute ownership of the XZ without the PS1 world).
  float commitX = 0.0f, commitZ = 0.0f;
  bool committed = false;
  float travel = 0.0f;  // planar units of the last step
  bool sampled = false;
  float x = 0.0f, z = 0.0f;
  // WalkStep state.
  ps1::NavState nav;
  bool strideMode = false;  // far branch: charge rearmed
  bool charged = false;     // TW_CHARGED
  bool charging = false;    // clip 0x11 running
  int chargeUpdates = 0;
  int chargeBlocked = 0;    // consecutive updates of the charge that covered no ground
  bool aim = false;
  int aimUpdates = 0;
  int aimReadyTick = 0;
  int side = 0;  // a sidestep is owed for the next attack (1 left, 2 right)
  bool steering = false;
  bool turning = false;   // the nav wants a turn on the spot: no step
  bool standing = false;  // the body does not travel: the idle clip replaces the walk
  bool walking = false;   // this update plans a step
  bool gameplay = false;  // the update is the gameplay one
  float hx = 0.0f, hz = 1.0f;
  float step = ps1::kTyrantWalkStep;
  int traces = 0;
  int frames = 1;  // gameplay frames between two updates of this actor
  uint32_t lastFrame = 0;
  float savedAim[3] = {};
  bool aimSaved = false;
};

std::unordered_map<void*, Track> g_tracks;
uint32_t g_tick = 0;
uint32_t g_lastShot = 0;

bool Objects(void* s, uint8_t*& o, uint8_t*& t) {
  auto* collection = game::Read<uint8_t*>(s, game::S::kCollection);
  if (!collection) return false;
  auto** objects = game::Read<uint8_t**>(collection, game::C::kObjects);
  if (!objects) return false;
  const int index = game::Read<int>(s, game::S::kIndex);
  const int target = game::Read<int>(collection, game::C::kTarget);
  if (index < 0 || target < 0 || index >= game::Read<int>(collection, game::C::kCount) ||
      target >= game::Read<int>(collection, game::C::kCount)) {
    return false;
  }
  o = objects[index];
  t = objects[target];
  return o && t && o != t;
}

float Planar(float x, float z) { return std::sqrt(x * x + z * z); }

void RestoreAim(Track& tr, uint8_t* t) {
  if (!tr.aimSaved) return;
  float* aim = game::Field<float>(t, game::O::kAimPoint);
  std::memcpy(aim, tr.savedAim, sizeof tr.savedAim);
  tr.aimSaved = false;
}

// O+D4 (this update's accumulation) and O+C8 (the damped velocity of 0045E730): neither may add a step.
void ClearPlanar(uint8_t* o) {
  float* accum = game::Field<float>(o, game::O::kAccum);
  accum[0] = accum[1] = accum[2] = 0.0f;
  float* velocity = game::Field<float>(o, 0xC8);
  velocity[0] = velocity[2] = 0.0f;
}

// Normalized forward, or the target when the matrix has no usable row.
void TurnTowards(const uint8_t* o, float tx, float tz, float& hx, float& hz) {
  const float len = Planar(tx, tz);
  if (len <= 0.0f) return;
  const float ux = tx / len, uz = tz / len;
  const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
  const float clen = Planar(fw[0], fw[2]);
  if (clen <= 0.0f) {
    hx = ux;
    hz = uz;
    return;
  }
  const float cx = fw[0] / clen, cz = fw[2] / clen;
  const float dot = cx * ux + cz * uz;
  const float cross = cx * uz - cz * ux;
  const float angle = std::atan2(cross, dot);
  if (std::fabs(angle) <= ps1::kTyrantTurnStep) {
    hx = ux;
    hz = uz;
    return;
  }
  const float a = angle > 0.0f ? ps1::kTyrantTurnStep : -ps1::kTyrantTurnStep;
  const float c = std::cos(a), sn = std::sin(a);
  hx = cx * c - cz * sn;
  hz = cx * sn + cz * c;
}

}  // namespace

// A shot left the player's weapon (quick_save.cpp, both native firing paths).
void OnPlayerShot() { g_lastShot = g_tick + 1; }

// Before the enemy logic of a gameplay update: the WalkStep decision for this one. `active` is the
// native gameplay gate: outside it the tyrant is not moved or re-aimed by the port.
void BeginUpdate(void* s, bool active) {
  if (!g_config.ps1Tyrant || !ps1::IsTyrant(game::Read<int>(s, game::S::kType))) {
    g_tracks.erase(s);
    return;
  }
  uint8_t* o = nullptr;
  uint8_t* t = nullptr;
  if (!Objects(s, o, t)) {
    g_tracks.erase(s);
    return;
  }
  const int index = game::Read<int>(s, game::S::kIndex);
  Track& tr = g_tracks[s];
  // S and O addresses are reused: a different object or slot restarts all PS1 state.
  if (tr.o != o || tr.index != index) {
    tr = Track();
    tr.o = o;
    tr.index = index;
  }
  ++g_tick;
  // Gameplay frames between two updates of this actor, from the shared PS1 cadence counter.
  uint32_t frame = 0;
  if (Ps1LogicFrame(&frame)) {
    if (tr.lastFrame != 0 && frame > tr.lastFrame) {
      tr.frames = static_cast<int>((std::min)(frame - tr.lastFrame, 4u));
    }
    tr.lastFrame = frame;
  }
  tr.gameplay = active;
  tr.walking = false;
  tr.turning = false;
  tr.steering = false;
  tr.side = 0;
  const int request = game::Read<int>(s, game::S::kRequest);
  const int selector = game::Read<int>(s, game::S::kSelector);
  if (!active || game::Read<float>(o, game::O::kHealth) <= 0.0f) {
    tr.walking = false;
    tr.charging = false;
    tr.aim = false;
    RestoreAim(tr, t);
    return;
  }

  const float* pos = reinterpret_cast<const float*>(o + game::O::kPos);
  // Without the PS1 world the native physics can add a step of its own between two updates: the
  // position written here last time is the only travel this actor is credited with.
  const bool owned = zombie::WorldOwnsObject(o);
  const float curX = owned || !tr.committed ? pos[0] : tr.commitX;
  const float curZ = owned || !tr.committed ? pos[2] : tr.commitZ;
  if (tr.sampled) tr.travel = Planar(curX - tr.x, curZ - tr.z);
  tr.x = curX;
  tr.z = curZ;
  tr.sampled = true;
  tr.standing = ps1::TyrantStanding(tr.standing, tr.travel);

  const float* tp = reinterpret_cast<const float*>(t + game::O::kPos);
  const float toX = tp[0] - pos[0];
  const float toZ = tp[2] - pos[2];
  const float distance = Planar(toX, toZ);
  const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
  const bool facing = ps1::TyrantFacing(fw[0], fw[2], toX, toZ);
  const bool locomotion = ps1::IsTyrantWalkRequest(request);

  // The far branch switches the parts to the stride and rearms the charge once; after that the roll
  // is made whenever the player is inside 6000, so a charge can start before he is close.
  if (ps1::TyrantEntersStride(tr.strideMode, distance)) {
    tr.strideMode = true;
    tr.charged = false;
  } else if (ps1::TyrantLeavesStride(distance)) {
    tr.strideMode = false;
  }
  if (tr.charging) {
    ++tr.chargeUpdates;
    const bool done = game::Read<uint8_t>(s, game::S::kDone) != 0;
    // The clip is reapplied on every update, which keeps its completion flag down: end on the port's
    // own counter, and drop the charge when it cannot advance (a wall or a body in front).
    if (tr.travel < 1.0f) ++tr.chargeBlocked; else tr.chargeBlocked = 0;
    if (tr.chargeUpdates < ps1::kTyrantChargeMinimum) {
      // keep charging
    } else if (done || tr.chargeUpdates >= ps1::kTyrantChargeUpdates ||
               tr.chargeBlocked >= ps1::kTyrantChargeBlocked) {
      tr.charging = false;
    }
  }
  if (!tr.charging && locomotion &&
      ps1::TyrantShouldCharge(tr.charged, distance, zombie::NextRngByte() % 100, facing)) {
    tr.charging = true;
    tr.charged = true;
    tr.chargeUpdates = 0;
    tr.chargeBlocked = 0;
    // The charge commits to its heading, like the PS1 action that does not turn.
    const float flen = Planar(fw[0], fw[2]);
    if (flen > 0.0f) {
      tr.hx = fw[0] / flen;
      tr.hz = fw[2] / flen;
    }
  }

  // Aim stop (PlayerFiring): far away, player in front, a shot just fired. Off by default,
  // the selector is unused by this model.
  if (!g_config.ps1TyrantExtras) {
    tr.aim = false;
  } else if (tr.aim) {
    if (++tr.aimUpdates >= ps1::kTyrantAimUpdates) tr.aim = false;
  } else {
    const bool shotRecently = g_lastShot != 0 && g_tick >= g_lastShot &&
                              static_cast<int>(g_tick - g_lastShot) <= ps1::kTyrantAimShotWindow;
    if (ps1::TyrantShouldAim(false, distance, facing, shotRecently,
                            static_cast<int>(g_tick) >= tr.aimReadyTick)) {
      tr.aim = true;
      tr.aimUpdates = 0;
      tr.aimReadyTick = static_cast<int>(g_tick) + ps1::kTyrantAimCooldown;
    }
  }

  // Sidestep: the player runs past while the tyrant faces them; the sign of the cross product is the
  // side (PlayerSide 1 -> clip 5, 2 -> clip 0x15).
  const float* tv = reinterpret_cast<const float*>(t + game::O::kVelocity);
  const bool playerMoving = Planar(tv[0], tv[2]) > 1.0f;
  const int side = ps1::TyrantSideFromLineSide(fw[0] * toZ - fw[2] * toX);
  if (g_config.ps1TyrantExtras &&
      ps1::TyrantCanSidestep(tr.charging || tr.aim, distance, facing, playerMoving, side)) {
    tr.side = side;
  }

  const bool walking = (locomotion || tr.charging) && !tr.aim &&
                       (selector == ps1::kTyrantIdleSelector || selector == ps1::kTyrantWalkSelector ||
                        selector == ps1::kTyrantChargeSelector || tr.charging);
  if (!walking) {
    RestoreAim(tr, t);
    return;
  }
  tr.walking = true;
  // 26 units per update for the walk: the walk clip is authored at that stride (S+40 = 3.0 is the PS1
  // 1x rate). Only the charge clip takes the doubled step.
  // The PS1 covers 26 units per frame. The PC services this actor at its own cadence, so the step
  // is multiplied by the frames elapsed since its last update: an actor updated every third frame
  // then covers three frames of ground instead of crawling.
  tr.step = ps1::TyrantStep(tr.charging) * static_cast<float>(tr.frames);
  if (tr.charging) return;  // the charge keeps the heading it committed to

  // Steer: the same PS1 nav the zombies use. It turns to the player, strides with a clear line and
  // goes round a wall; while it asks for a turn on the spot there is no step (8007F210).
  const auto& walls = CurrentRoomWalls();
  ps1::NavParams params;
  if (!walls.empty()) {
    params.ps1IntegerSteer = true;
    params.ps1Speed = -static_cast<int>(tr.step);
    params.turnStep = ps1::kTurnStep;
    params.rateScale = 1.0f;
    params.playerWallMargin = 0.0f;
  } else {
    params.turnStep = g_config.navTurnStep * 3.14159265f / 180.0f;
    params.rateScale = g_config.navRateScale;
  }
  const ps1::NavOutput nav =
      ps1::NavStep(tr.nav, walls, {{pos[0], -pos[2]}, {fw[0], -fw[2]}, {tp[0], -tp[2]}}, params);
  tr.hx = nav.heading.x;
  tr.hz = -nav.heading.z;
  tr.turning = !nav.walk;
  tr.steering = nav.walk && !walls.empty() &&
                ps1::FirstBlockingWall(walls, {pos[0], -pos[2]}, {tp[0], -tp[2]}) >= 0;
  if (tr.turning) {
    tr.step = 0.0f;  // turning on the spot
    tr.standing = false;
  }
  if (tr.steering) {
    float* aim = game::Field<float>(t, game::O::kAimPoint);
    if (!tr.aimSaved) {
      std::memcpy(tr.savedAim, aim, sizeof tr.savedAim);
      tr.aimSaved = true;
    }
    aim[0] = pos[0] + tr.hx * g_config.navAimDistance;
    aim[2] = pos[2] + tr.hz * g_config.navAimDistance;
    return;
  }
  RestoreAim(tr, t);
  // With a clear line the PS1 turns to the player at 0x30 angle units per frame; the nav turn while
  // going round a wall is the same limit.
  if (!tr.steering) TurnTowards(o, toX, toZ, tr.hx, tr.hz);
}

// After the native update: the planned step replaces whatever the enemy logic did with the body.
void Finish(void* s) {
  if (!g_config.ps1Tyrant) return;
  auto it = g_tracks.find(s);
  if (it == g_tracks.end()) return;
  Track& tr = it->second;
  const bool gameplay = tr.gameplay;
  tr.gameplay = false;
  if (!gameplay) return;
  uint8_t* o = nullptr;
  uint8_t* t = nullptr;
  if (!Objects(s, o, t)) return;
  RestoreAim(tr, t);
  if (tr.aim) {
    ClearPlanar(o);  // PlayerFiring: the tyrant stands and aims
    tr.committed = false;
    return;
  }
  if (!tr.walking) return;
  zombie::SetHeading(o, tr.hx, tr.hz);
  ClearPlanar(o);
  if (zombie::WorldOwnsObject(o)) {
    // Committed by WorldFinishUpdate -> StepZombieWorld, which slides along the ROOMENV walls (a plain
    // straight-line check would pin him against a crate), and keeps the actor and player contacts.
    zombie::MoveZombie(o, tr.hx * tr.step, tr.hz * tr.step);
    tr.committed = false;
    return;
  }
  // No PS1 world for this room: own the XZ absolutely, so the native physics cannot add a step.
  float* pos = game::Field<float>(o, game::O::kPos);
  if (!tr.committed) {
    tr.commitX = pos[0];
    tr.commitZ = pos[2];
    tr.committed = true;
  }
  float nx = tr.commitX + tr.hx * tr.step;
  float nz = tr.commitZ + tr.hz * tr.step;
  const auto& walls = CurrentRoomWalls();
  if (!walls.empty() &&
      ps1::FirstBlockingWall(walls, {tr.commitX, -tr.commitZ}, {nx, -nz}) >= 0) {
    nx = tr.commitX;
    nz = tr.commitZ;
  }
  tr.commitX = nx;
  tr.commitZ = nz;
  pos[0] = nx;
  pos[2] = nz;
  // Diagnostic: travel against step and clip rate, plus the update cadence, tell whether the body and
  // the animation agree. Only written when logging is enabled.
  if ((++tr.traces % 30) == 1) {
    Log("Tyrant: S=%p sel=%d req=%d step=%.1f travel=%.1f frames=%d rate=%.2f x=%.0f z=%.0f", s,
        game::Read<int>(s, game::S::kSelector), game::Read<int>(s, game::S::kRequest), tr.step, tr.travel,
        tr.frames, game::Read<float>(s, game::S::kNextSpeed), nx, nz);
  }
}

// Runs where the native mapper result is final. Death, aim, charge and sidestep pick their PS1 clip;
// the walk keeps the authored 1x rate.
void Map(void* s, int request, int& result) {
  if (!g_config.ps1Tyrant || !ps1::IsTyrant(game::Read<int>(s, game::S::kType))) return;
  if (ps1::IsTyrantDeathRequest(request)) {
    result = ps1::kTyrantDeathSelector;
    return;
  }
  auto it = g_tracks.find(s);
  if (it == g_tracks.end()) return;
  Track& t = it->second;
  if (g_config.ps1TyrantExtras && t.aim && ps1::IsTyrantWalkRequest(request)) {
    *game::Field<float>(s, game::S::kNextSpeed) = ps1::kTyrantWalkSpeed;
    result = ps1::kTyrantAimSelector;
    return;
  }
  if (t.charging && ps1::IsTyrantWalkRequest(request)) {
    *game::Field<float>(s, game::S::kNextSpeed) = ps1::kTyrantWalkSpeed;
    result = ps1::kTyrantChargeSelector;
    return;
  }
  if (g_config.ps1TyrantExtras && request == 8 && t.side != 0) {
    result = ps1::TyrantSideSelector(t.side);
    t.side = 0;
    return;
  }
  if (!ps1::IsTyrantWalkRequest(request) || result != ps1::kTyrantWalkSelector) return;
  *game::Field<float>(s, game::S::kNextSpeed) = ps1::kTyrantWalkSpeed;
  result = ps1::TyrantWalkSelector(t.standing);
}

void Reset() {
  g_tracks.clear();
  g_tick = 0;
  g_lastShot = 0;
}

}  // namespace sf::features::tyrant
