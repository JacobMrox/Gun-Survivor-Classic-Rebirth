#include <cstdio>
#include "../source/survivor_fix/ps1/cerberus_logic.h"

using namespace ps1::cerberus;
int main() {
  int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
  CHECK(DeathSelector(11, 15, 15, 4, true) == 15);
  CHECK(DeathSelector(11, 16, 15, 4, true) == 15);
  CHECK(DeathSelector(11, 15, 15, 4, false) == 4);
  CHECK(DeathSelector(10, 15, 15, 4, true) == 4);
  CHECK(DeathSelector(11, 2, 15, 4, true) == 4);
  CHECK(DeathSelector(11, 15, 1, 15, true) == 15);
  CHECK(ApproachSelector(11, 5, 11, true, true) == 2);
  CHECK(ApproachSelector(11, 6, 10, true, true) == 2);
  CHECK(ApproachSelector(11, 9, 17, true, true) == 17);
  CHECK(ApproachSelector(11, 5, 11, false, true) == 11);
  CHECK(ApproachSelector(11, 6, 10, true, false) == 10);
  CHECK(ApproachSelector(10, 5, 11, true, true) == 11);
  CHECK(SignedYawDelta(4090, 10) == 16);
  CHECK(SignedYawDelta(10, 4090) == -16);
  CHECK(SteerYaw(100, 144, 64, true) == 100);
  CHECK(SteerYaw(100, 145, 64, true) == 164);
  CHECK(SteerYaw(100, 55, 64, true) == 36);
  CHECK(SteerYaw(4090, 100, 64, false) == 15);
  CHECK(ClearApproachEligible(11, true, true, true, false, 4001, 5, 6));
  CHECK(!ClearApproachEligible(11, true, true, true, false, 4000, 5, 6));
  CHECK(!ClearApproachEligible(11, true, true, true, false, 6000, 5, 6));
  CHECK(!ClearApproachEligible(11, true, true, true, true, 5000, 5, 6));
  CHECK(!ClearApproachEligible(11, true, true, false, false, 5000, 5, 6));
  CHECK(!ClearApproachEligible(11, true, true, true, false, 5000, 5, 9));
  CHECK(!ClearApproachEligible(10, true, true, true, false, 5000, 5, 6));
  CHECK(AvoidWallSide(1, -1, -1, -1, 0, 0) == 0);
  CHECK(AvoidWallSide(2, 4, 4, -1, 0, 0) == 2);
  CHECK(AvoidWallSide(0, 4, 4, -1, 0, 0) == 1);
  CHECK(AvoidWallSide(0, 4, -1, 4, 0, 0) == 2);
  CHECK(AvoidWallSide(0, 4, -1, -1, 0, 1) == 2);
  CHECK(AvoidWallSide(0, 4, 5, 6, 999, 0) == 1);
  CHECK(AvoidWallSide(0, 4, 5, 6, 1000, 1) == 0);
  CHECK(!PcWallStuck(false, 0.0f));
  CHECK(PcWallStuck(true, 1.0f));
  CHECK(PcWallStuck(true, 2.0f));
  CHECK(!PcWallStuck(true, 15.0f));
  CHECK(!PcWallStuck(true, 50.0f));
  // Sleep/alert gating (RoamStep: alert on gunfire or dist < 6001; the
  // wrapper wakes on damage or proximity).
  CHECK(ShouldWake(5999.0f, false));
  CHECK(!ShouldWake(6000.0f, false));
  CHECK(ShouldWake(9000.0f, true));
  // Chase speed follows facing alignment (no sliding while turning).
  CHECK(ChaseSpeedFactor(1.0f) == 1.0f);
  CHECK(ChaseSpeedFactor(0.0f) == 0.0f);
  CHECK(ChaseSpeedFactor(-0.5f) == 0.0f);
  CHECK(ChaseSpeedFactor(2.0f) == 1.0f);
  CHECK(ChaseSpeedFactor(0.5f) == 0.5f);
  CHECK(RoamAlert(1, 6000, false) == RoamTransition::Chase);
  CHECK(RoamAlert(1, 6001, false) == RoamTransition::None);
  CHECK(RoamAlert(1, 9000, true) == RoamTransition::Chase);
  CHECK(RoamAlert(3, 999, false) == RoamTransition::ClosePlayer);
  CHECK(RoamAlert(3, 1000, true) == RoamTransition::None);
  CHECK(!CanLeap(3999, 0, true, true));
  CHECK(CanLeap(4000, 0, true, true));
  CHECK(CanLeap(6000, 0, true, true));
  CHECK(!CanLeap(6001, 0, true, true));
  CHECK(!CanLeap(5000, 1, true, true));
  CHECK(!CanLeap(5000, 0, false, true));
  CHECK(!CanLeap(5000, 0, true, false));
  ChaseState s;
  ChaseInput in;
  in.distance = 3500;
  CHECK(ChaseStep(s, in) == ChaseAction::Wander && s.wander);
  in.distance = 6100;
  CHECK(ChaseStep(s, in) == ChaseAction::Wander && !s.wander);
  in.seesPlayer = true;
  CHECK(ChaseStep(s, in) == ChaseAction::Circle);
  in.distance = 5000; in.facingTarget = true; in.playerInLeapCone = true;
  CHECK(ChaseStep(s, in) == ChaseAction::Leap);
  in.wallBetween = true;
  CHECK(ChaseStep(s, in) == ChaseAction::AvoidWall);
  in.dodgeCooldown = 2;
  CHECK(ChaseStep(s, in) == ChaseAction::Dodge);
  in.dodgeCooldown = 0; in.leapState = 1;
  CHECK(ChaseStep(s, in) == ChaseAction::ContinueLeap);
  if (failures) return 1;
  std::puts("PASS: partial PS1 Cerberus decisions and boundaries");
  return 0;
}
