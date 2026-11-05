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
