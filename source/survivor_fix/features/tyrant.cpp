// Mr. X (PC types 22/23): the PS1 walk-step behaviour (port/decomp/tyrant.c, n_tyrant_WalkStep
// 0x80101404) on top of the clip choice of ps1/tyrant_logic.h.
//   - the walk clip follows the travel the native physics produced, so the feet stay on the ground;
//   - with a ROOMENV wall between the tyrant and the player the native turn is aimed along the shared
//     PS1 nav heading (ps1/nav_logic.h, the same NavStep the zombies use), i.e. it goes round the wall;
//   - the charge (clip 0x11, a 40 unit impulse like the PC's own charge case, one roll per approach);
//   - the sidestep: a swing to the player's side (clip 5 or 0x15) when they run past.
// Nothing else is replaced: the native enemy logic still owns the movement, its collisions, the
// attacks, the hit event and the death; the aim point of the player is restored after every update.
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
  bool standing = false;
  bool sampled = false;
  // WalkStep state.
  ps1::NavState nav;
  bool charged = false;   // TW_CHARGED: rolled since the last far approach
  bool charging = false;  // clip 0x11 running
  int chargeUpdates = 0;
  int side = 0;  // a sidestep is owed for the next attack (1 left, 2 right)
  bool steering = false;
  float hx = 0.0f, hz = 1.0f;
  float savedAim[3] = {};
  bool aimSaved = false;
};

std::unordered_map<void*, Track> g_tracks;

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

}  // namespace

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

  const float* pos = reinterpret_cast<const float*>(o + game::O::kPos);
  if (tr.sampled) tr.travel = Planar(pos[0] - tr.x, pos[2] - tr.z);
  tr.x = pos[0];
  tr.z = pos[2];
  tr.sampled = true;

  tr.side = 0;
  const int request = game::Read<int>(s, game::S::kRequest);
  const int selector = game::Read<int>(s, game::S::kSelector);
  if (game::Read<float>(o, game::O::kHealth) <= 0.0f) {
    tr.charging = false;
    tr.steering = false;
    RestoreAim(tr, t);
    return;
  }

  const float* tp = reinterpret_cast<const float*>(t + game::O::kPos);
  const float toX = tp[0] - pos[0];
  const float toZ = tp[2] - pos[2];
  const float distance = Planar(toX, toZ);
  const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
  const bool facing = ps1::TyrantFacing(fw[0], fw[2], toX, toZ);

  // WalkStep: the roll is rearmed once the player is beyond the stride range. The charge clip ends
  // when its sequence completes (S+2D), like the PS1 returning from mode 2. The end of a running
  // charge is tracked even in the update in which the enemy logic asks for something else.
  if (ps1::TyrantResetsCharge(distance)) {
    tr.charged = false;
    tr.charging = false;
  }
  if (tr.charging && (++tr.chargeUpdates > 300 || game::Read<uint8_t>(s, game::S::kDone) != 0)) {
    tr.charging = false;
  }
  if (!tr.charging && ps1::IsTyrantWalkRequest(request) &&
      ps1::TyrantShouldCharge(tr.charged, distance, zombie::NextRngByte() % 100, facing)) {
    tr.charging = true;
    tr.charged = true;
    tr.chargeUpdates = 0;
    // 0043A364: the native charge case adds 40 units of local forward movement once.
    zombie::g_accumulate(o, nullptr, 0.0f, 0.0f, ps1::kTyrantChargeImpulse);
  }

  // Sidestep: the player runs past while the tyrant faces them; the sign of the cross product is the
  // side (PlayerSide 1 -> clip 5, 2 -> clip 0x15). One sidestep is owed per run-past, and the attack
  // request of this same update consumes it. It is decided also in an attack update, like the PS1
  // WalkStep, which sets TW_DODGED before the attack clip runs.
  const float* tv = reinterpret_cast<const float*>(t + game::O::kVelocity);
  const bool playerMoving = Planar(tv[0], tv[2]) > 1.0f;
  const int side = ps1::TyrantSideFromLineSide(fw[0] * toZ - fw[2] * toX);
  if (ps1::TyrantCanSidestep(tr.charging, distance, facing, playerMoving, side)) tr.side = side;

  // Steer: the PS1 turns to the player, strides on until a wall is ahead and then goes round it. The
  // native turn follows the player's aim point, so steering aims it along the nav heading instead.
  tr.steering = false;
  const auto& walls = CurrentRoomWalls();
  const bool walking = ps1::IsTyrantWalkRequest(request) &&
                       (selector == ps1::kTyrantIdleSelector || selector == ps1::kTyrantWalkSelector ||
                        selector == ps1::kTyrantChargeSelector || tr.charging);
  if (!walking) {
    RestoreAim(tr, t);
    return;
  }
  if (!walls.empty() && ps1::FirstBlockingWall(walls, {pos[0], -pos[2]}, {tp[0], -tp[2]}) >= 0) {
    ps1::NavParams params;
    params.ps1IntegerSteer = true;
    params.ps1Speed = -static_cast<int>(ps1::TyrantStep(ps1::TyrantStriding(distance)));
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
  }
  if (!tr.steering) RestoreAim(tr, t);
}

// After the native update: the steer heading replaces the one the enemy logic added, and the player's
// aim point goes back to what the rest of the room expects.
void Finish(void* s) {
  if (!g_config.ps1Tyrant) return;
  auto it = g_tracks.find(s);
  if (it == g_tracks.end()) return;
  Track& tr = it->second;
  uint8_t* o = nullptr;
  uint8_t* t = nullptr;
  if (!Objects(s, o, t)) return;
  RestoreAim(tr, t);
  if (tr.steering) zombie::SetHeading(o, tr.hx, tr.hz);
}

// Runs where the native mapper result is final. Death, charge and sidestep pick their PS1 clip; the
// walk picks the idle/walk clip and its speed from the measured travel. Every other request keeps the
// PC's own selector, threshold and speed.
void Map(void* s, int request, int& result) {
  if (!g_config.ps1Tyrant || !ps1::IsTyrant(game::Read<int>(s, game::S::kType))) return;
  if (ps1::IsTyrantDeathRequest(request)) {
    result = ps1::kTyrantDeathSelector;
    return;
  }
  auto it = g_tracks.find(s);
  if (it == g_tracks.end()) return;
  Track& t = it->second;
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
  t.standing = ps1::TyrantStanding(t.standing, t.travel);
  *game::Field<float>(s, game::S::kNextSpeed) =
      t.standing ? ps1::kTyrantIdleSpeed : ps1::TyrantWalkClipSpeed(t.travel);
  result = ps1::TyrantWalkSelector(t.standing);
}

void Reset() { g_tracks.clear(); }

}  // namespace sf::features::tyrant
