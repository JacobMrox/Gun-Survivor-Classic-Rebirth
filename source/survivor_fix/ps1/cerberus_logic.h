// Partial, platform-neutral Cerberus decision model. Not yet connected to PC actors.
// Behavior reference: RESPeCt port/decomp/cerberus.c, revision
// cf880a8d496303ce10f8f08738f320afe01cfe01 (GPL-3.0), functions
// cerberus_RoamStep, cerberus_ChaseStep and cerberus_CanLeap.
#pragma once

namespace ps1::cerberus {

enum class RoamTransition { None, Chase, ClosePlayer };

// PC type 11 selects 15 for the fall, then selects 4 after the completion
// gate. Keep the fall selected once a track-holding adapter owns this life;
// the native mapper must still run for its kill/pool side effects.
constexpr int DeathSelector(int type, int request, int current, int native, bool tracksOwned) {
  return type == 11 && (request == 15 || request == 16) && current == 15 && native == 4 && tracksOwned
      ? 15 : native;
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
