// Mr. X (PC types 22/23): the PS1 walk step (port/decomp/tyrant.c, n_tyrant_WalkStep 0x80101404).
//   - the body moves 26 units per update (52 while charging) along a heading turned at most 4.2 degrees
//     per update, with the room's ROOMENV walls, like 8007F404; the native accumulation is cleared, so
//     the walk clip keeps its authored 1x rate and the feet stay on the ground;
//   - with a wall between the tyrant and the player the heading comes from the shared PS1 nav logic
//     (ps1/nav_logic.h, NavStep): it turns, strides and goes round the wall;
//   - the charge (clip 0x11) rolls on the PS1 band (inside 6000, one 25% roll per approach, player in
//     front) and holds for at least a few steps;
//   - the aim stop (clip 0x13) when the player fires while the tyrant is far, like PlayerFiring;
//   - the sidestep: a swing to the player's side (clip 5 or 0x15) when they run past.
// The native enemy logic still picks the attacks and owns the hit event, the damage and the death; the
// steer aim point of the player is restored after every update.
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
  float x = 0.0f, z = 0.0f;
  float travel = 0.0f;  // planar units between two updates
  bool sampled = false;
  // WalkStep state.
  ps1::NavState nav;
  bool strideMode = false;  // the far branch: parts' direction 0x20, charge rearmed
  bool charged = false;     // TW_CHARGED
  bool charging = false;    // clip 0x11 running
  int chargeUpdates = 0;
  bool aim = false;
  int aimUpdates = 0;
  int aimReadyTick = 0;
  int side = 0;  // a sidestep is owed for the next attack (1 left, 2 right)
  bool steering = false;
  bool standing = false;  // the body does not travel: the idle clip replaces the walk
  bool walking = false;   // this update moves the body
  float hx = 0.0f, hz = 1.0f;
  float step = ps1::kTyrantWalkStep;
  float scale = 1.0f;  // fallback: scale of the native accumulation without PS1 walls
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

void ClearAccum(uint8_t* o) {
  float* accum = game::Field<float>(o, game::O::kAccum);
  accum[0] = accum[1] = accum[2] = 0.0f;
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
  // A step towards the target in the same sense as the cross product.
  const float a = angle > 0.0f ? ps1::kTyrantTurnStep : -ps1::kTyrantTurnStep;
  const float c = std::cos(a), sn = std::sin(a);
  hx = cx * c - cz * sn;
  hz = cx * sn + cz * c;
}

}  // namespace

// A shot left the player's weapon (quick_save.cpp, both native firing paths).
void OnPlayerShot() { g_lastShot = g_tick + 1; }

// Before the enemy logic of every gameplay update: one travel sample (the physics applied the previous
// update's movement) and the WalkStep decision for this one.
void BeginUpdate(void* s) {
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

  const float* pos = reinterpret_cast<const float*>(o + game::O::kPos);
  if (tr.sampled) tr.travel = Planar(pos[0] - tr.x, pos[2] - tr.z);
  tr.x = pos[0];
  tr.z = pos[2];
  tr.sampled = true;
  tr.standing = ps1::TyrantStanding(tr.standing, tr.travel);

  tr.side = 0;
  tr.walking = false;
  tr.steering = false;
  const int request = game::Read<int>(s, game::S::kRequest);
  const int selector = game::Read<int>(s, game::S::kSelector);
  if (game::Read<float>(o, game::O::kHealth) <= 0.0f) {
    tr.charging = false;
    tr.aim = false;
    RestoreAim(tr, t);
    return;
  }

  const float* tp = reinterpret_cast<const float*>(t + game::O::kPos);
  const float toX = tp[0] - pos[0];
  const float toZ = tp[2] - pos[2];
  const float distance = Planar(toX, toZ);
  const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
  const bool facing = ps1::TyrantFacing(fw[0], fw[2], toX, toZ);
  const bool locomotion = ps1::IsTyrantWalkRequest(request);

  // WalkStep: the far branch switches the parts to the stride and rearms the charge once; after that
  // the roll is made whenever the player is inside 6000, so a charge can start before he is close.
  if (ps1::TyrantEntersStride(tr.strideMode, distance)) {
    tr.strideMode = true;
    tr.charged = false;
  } else if (ps1::TyrantLeavesStride(distance)) {
    tr.strideMode = false;
  }
  if (tr.charging) {
    ++tr.chargeUpdates;
    const bool done = game::Read<uint8_t>(s, game::S::kDone) != 0;
    if ((done && tr.chargeUpdates >= ps1::kTyrantChargeMinimum) ||
        tr.chargeUpdates > ps1::kTyrantChargeTimeout) {
      tr.charging = false;
    }
  }
  if (!tr.charging && locomotion &&
      ps1::TyrantShouldCharge(tr.charged, distance, zombie::NextRngByte() % 100, facing)) {
    tr.charging = true;
    tr.charged = true;
    tr.chargeUpdates = 0;
    // The charge commits to its heading, like the PS1 action that does not turn.
    const float flen = Planar(fw[0], fw[2]);
    if (flen > 0.0f) {
      tr.hx = fw[0] / flen;
      tr.hz = fw[2] / flen;
    }
    // 0043A364: the native charge case adds 40 units of local forward movement once.
    zombie::g_accumulate(o, nullptr, 0.0f, 0.0f, ps1::kTyrantChargeImpulse);
  }

  // Aim stop (PlayerFiring): far away, player in front, a shot just fired.
  if (tr.aim) {
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
  // side (PlayerSide 1 -> clip 5, 2 -> clip 0x15). One sidestep is owed per run-past, and the attack
  // request of this same update consumes it.
  const float* tv = reinterpret_cast<const float*>(t + game::O::kVelocity);
  const bool playerMoving = Planar(tv[0], tv[2]) > 1.0f;
  const int side = ps1::TyrantSideFromLineSide(fw[0] * toZ - fw[2] * toX);
  if (ps1::TyrantCanSidestep(tr.charging || tr.aim, distance, facing, playerMoving, side)) tr.side = side;

  // Steer: the PS1 turns to the player, strides on until a wall is ahead and then goes round it. The
  // native turn follows the player's aim point, so steering aims it along the nav heading instead.
  const bool walking = (locomotion || tr.charging) && !tr.aim &&
                       (selector == ps1::kTyrantIdleSelector || selector == ps1::kTyrantWalkSelector ||
                        selector == ps1::kTyrantChargeSelector || tr.charging);
  if (!walking) {
    RestoreAim(tr, t);
    return;
  }
  tr.walking = true;
  // 26 units per update for the walk: the walk clip is authored at that stride (S+40 = 3.0 is the PS1
  // 1x rate), so a faster step would skate again. Only the charge clip takes the doubled step.
  tr.step = ps1::TyrantStep(tr.charging);
  if (tr.charging) return;  // the charge keeps the heading it committed to

  const auto& walls = CurrentRoomWalls();
  if (!walls.empty() && ps1::FirstBlockingWall(walls, {pos[0], -pos[2]}, {tp[0], -tp[2]}) >= 0) {
    ps1::NavParams params;
    params.ps1IntegerSteer = true;
    params.ps1Speed = -static_cast<int>(tr.step);
    params.turnStep = ps1::kTurnStep;
    params.rateScale = 1.0f;
    params.playerWallMargin = 0.0f;
    const ps1::NavOutput nav =
        ps1::NavStep(tr.nav, walls, {{pos[0], -pos[2]}, {fw[0], -fw[2]}, {tp[0], -tp[2]}}, params);
    tr.hx = nav.heading.x;
    tr.hz = -nav.heading.z;
    tr.steering = true;
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
  TurnTowards(o, toX, toZ, tr.hx, tr.hz);
}

// After the native update: the PS1 step (8007F404) replaces the movement the enemy logic accumulated,
// and the steer heading replaces the turn it added.
void Finish(void* s) {
  if (!g_config.ps1Tyrant) return;
  auto it = g_tracks.find(s);
  if (it == g_tracks.end()) return;
  Track& tr = it->second;
  uint8_t* o = nullptr;
  uint8_t* t = nullptr;
  if (!Objects(s, o, t)) return;
  RestoreAim(tr, t);
  if (tr.aim) {
    ClearAccum(o);  // PlayerFiring: the tyrant stands and aims
    return;
  }
  if (!tr.walking) return;
  zombie::SetHeading(o, tr.hx, tr.hz);
  const auto& walls = CurrentRoomWalls();
  if (!walls.empty()) {
    // The PS1 clears the velocity every frame (8007F404) and moves |E+866| along the heading; the
    // ROOMENV walls stop it. Keeping the native accumulation would add the port's own step on top.
    ClearAccum(o);
    zombie::MoveZombie(o, tr.hx * tr.step, tr.hz * tr.step);
    return;
  }
  // No PS1 walls in this room: keep the native collision and scale its own accumulation towards the
  // same step, measured from the travel of the previous update.
  float* accum = game::Field<float>(o, game::O::kAccum);
  if (tr.travel > 0.5f) {
    float factor = tr.step / tr.travel;
    factor = (std::max)(0.1f, (std::min)(2.0f, factor));
    tr.scale = tr.scale * 0.6f + factor * 0.4f;
  }
  accum[0] *= tr.scale;
  accum[2] *= tr.scale;
}

// Runs where the native mapper result is final. Death, aim, charge and sidestep pick their PS1 clip;
// the walk keeps the authored 1x rate. Every other request keeps the PC's own selector, threshold and
// speed.
void Map(void* s, int request, int& result) {
  if (!g_config.ps1Tyrant || !ps1::IsTyrant(game::Read<int>(s, game::S::kType))) return;
  if (ps1::IsTyrantDeathRequest(request)) {
    result = ps1::kTyrantDeathSelector;
    return;
  }
  auto it = g_tracks.find(s);
  if (it == g_tracks.end()) return;
  Track& t = it->second;
  if (t.aim && ps1::IsTyrantWalkRequest(request)) {
    *game::Field<float>(s, game::S::kNextSpeed) = ps1::kTyrantWalkSpeed;
    result = ps1::kTyrantAimSelector;
    return;
  }
  if (t.charging && ps1::IsTyrantWalkRequest(request)) {
    *game::Field<float>(s, game::S::kNextSpeed) = ps1::kTyrantWalkSpeed;
    result = ps1::kTyrantChargeSelector;
    return;
  }
  if (request == 8 && t.side != 0) {
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
