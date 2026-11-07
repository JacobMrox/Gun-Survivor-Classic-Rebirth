// Partial, platform-neutral Cerberus decision model. Not yet connected to PC actors.
// Behavior reference: RESPeCt port/decomp/cerberus.c, revision
// cf880a8d496303ce10f8f08738f320afe01cfe01 (GPL-3.0), functions
// cerberus_RoamStep, cerberus_ChaseStep and cerberus_CanLeap.
#pragma once

namespace ps1::cerberus {

// PS1 RoamStep (8010176C): roaming dogs switch to chase when the player fires
// with the sight on screen, or when the player is closer than 6001. PC units
// are the PS1 units (x identity, z negated only, per the voice/placement match).
constexpr float kWakeDistance = 6000.0f;
constexpr bool ShouldWake(float distance, bool damaged) {
  return damaged || distance < kWakeDistance;
}

// Experimental PC-only speed adjustment, not a PS1 chase rule. The user
// reports that this does not fix the run animation / apparent sliding.
constexpr float ChaseSpeedFactor(float cosAngle) {
  return cosAngle >= 1.0f ? 1.0f : (cosAngle <= 0.0f ? 0.0f : cosAngle);
}

enum class RoamTransition { None, Chase, ClosePlayer };

// 80100870: yaw uses 4096 units per revolution. In PS1 coordinates, the
// caller supplies targetYaw obtained from the game's facing/target convention.
// The original tests a +/-0x2c cone, then turns by the specified step (one
// third when the player is not visible); it does not snap to a fixed angle.
// This isolates the turning decision only, not sway, wall checks or movement.
constexpr int SignedYawDelta(int yaw, int targetYaw) {
  const unsigned wrapped = (static_cast<unsigned>(targetYaw) - static_cast<unsigned>(yaw)) & 4095u;
  return wrapped >= 2048u ? static_cast<int>(wrapped) - 4096 : static_cast<int>(wrapped);
}
constexpr int SteerYaw(int yaw, int targetYaw, int turn, bool seesPlayer) {
  const int delta = SignedYawDelta(yaw, targetYaw);
  if (delta >= -0x2c && delta <= 0x2c) return yaw & 4095;
  const int amount = seesPlayer ? turn : turn / 3;
  return (yaw + (delta > 0 ? amount : -amount)) & 4095;
}

// Narrow PC bridge gate for a first clear-path steering experiment. The PS1
// chase has other branches below 4001 and at/above 6000; leave those native.
constexpr bool ClearApproachEligible(int type, bool awake, bool alive, bool geometryReady,
                                     bool blocked, float distance, int before, int after) {
  return type == 11 && awake && alive && geometryReady && !blocked &&
      distance >= 4001.0f && distance < 6000.0f &&
      before >= 2 && before <= 6 && after >= 2 && after <= 6;
}

// 80100454: choose and retain the side while a wall is ahead. Inputs are
// wall indices from the original one-sided crossed-wall tests (-1 = clear).
// randomBit is the already-drawn low RNG bit, used only on random branches.
constexpr int AvoidWallSide(int current, int ahead, int left, int right,
                            int distanceToWall, int randomBit) {
  if (ahead == -1) return 0;
  if (current != 0) return current;
  if (ahead == left) return 1;
  if (ahead == right) return 2;
  if (left == -1 && right != -1) return 2;
  if (left != -1 && right == -1) return 1;
  if (left == -1 && right == -1) return (randomBit & 1) + 1;
  return distanceToWall < 1000 ? (randomBit & 1) + 1 : 0;
}

// PC compatibility: native movement has not been calibrated to PS1 run
// speed, so the original 81-unit threshold would classify ordinary PC steps
// as stuck. Only treat an essentially stationary native actor as stuck.
constexpr bool PcWallStuck(bool hadPosition, float moved) {
  return hadPosition && moved < 10.0f;
}

// PC type 11 selects 15 for the fall, then selects 4 after the completion
// gate. Keep the fall selected once a track-holding adapter owns this life;
// the native mapper must still run for its kill/pool side effects.
constexpr int DeathSelector(int type, int request, int current, int native, bool tracksOwned) {
  return type == 11 && (request == 15 || request == 16) && current == 15 && native == 4 && tracksOwned
      ? 15 : native;
}

// Experimental PC adaptation: requests 5/6 choose turning clips 11/10 even
// while the actor advances. Keep attacks and all other mapper results intact.
constexpr int ApproachSelector(int type, int request, int native, bool awake, bool alive) {
  return type == 11 && awake && alive &&
      ((request == 5 && native == 11) || (request == 6 && native == 10)) ? 2 : native;
}

// Player firing only counts when the sight is on-screen. Mode 3 has its
// own close-player transition and does not use the ordinary alert branch.
constexpr RoamTransition RoamAlert(int mode, int distance, bool firingWithSight) {
  if (mode == 3) return distance < 1000 ? RoamTransition::ClosePlayer : RoamTransition::None;
  return (firingWithSight || distance < 6001) ? RoamTransition::Chase : RoamTransition::None;
}

constexpr bool CanLeap(int distance, int turnCooldown, bool facingTarget, bool playerInCone) {
  return turnCooldown == 0 && facingTarget && distance >= 4000 && distance < 6001 && playerInCone;
}

enum class ChaseAction { Dodge, ContinueLeap, HoldLeap, AvoidWall, Wander, LeadPlayer, CooldownSteer, Circle, Leap, Steer };

struct ChaseState { bool wander = false; };
struct ChaseInput {
  int distance = 0;
  int dodgeCooldown = 0;
  int leapState = 0;
  int turnCooldown = 0;
  bool wallBetween = false;
  bool seesPlayer = false;
  bool facingTarget = false;
  bool playerInLeapCone = false;
};

// The caller must supply PS1-space distance, wall and cone measurements.
// These decisions alone do not move an actor or drive PC animation events.
inline ChaseAction ChaseStep(ChaseState& state, const ChaseInput& in) {
  if (in.distance < 4001) state.wander = true;
  if (in.dodgeCooldown != 0) return ChaseAction::Dodge;
  if (in.leapState == 1) return ChaseAction::ContinueLeap;
  if (in.leapState != 0) return ChaseAction::HoldLeap;
  if (in.wallBetween) return ChaseAction::AvoidWall;
  if (state.wander) {
    if (in.distance >= 6000) state.wander = false;
    return ChaseAction::Wander;
  }
  if (!in.seesPlayer) return ChaseAction::LeadPlayer;
  if (in.turnCooldown != 0) return ChaseAction::CooldownSteer;
  if (in.distance >= 6000) return ChaseAction::Circle;
  return CanLeap(in.distance, in.turnCooldown, in.facingTarget, in.playerInLeapCone)
      ? ChaseAction::Leap : ChaseAction::Steer;
}

}  // namespace ps1::cerberus
