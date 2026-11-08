#include <cstdio>
#include "../source/survivor_fix/ps1/crow_logic.h"

using namespace ps1::crow;
int main() {
  int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
  CHECK(kCrowType == 12);
  CHECK(kAnimFly == 4);
  CHECK(IsLocomotionRequest(2));
  CHECK(IsLocomotionRequest(4));
  CHECK(IsLocomotionRequest(7));
  CHECK(!IsLocomotionRequest(0));
  CHECK(!IsLocomotionRequest(1));
  CHECK(!IsLocomotionRequest(8));
  // Crow in flight keeps flying clip (ApproachSelector).
  CHECK(ApproachSelector(12, 2, 1, true) == 4);
  CHECK(ApproachSelector(12, 5, 11, true) == 4);
  CHECK(ApproachSelector(12, 5, 4, true) == 4);
  CHECK(ApproachSelector(12, 0, 1, true) == 1);
  CHECK(ApproachSelector(12, 5, 3, true) == 4);
  CHECK(ApproachSelector(12, 5, 11, false) == kAnimDeadFall);
  CHECK(ApproachSelector(11, 5, 11, true) == 11);
  CHECK(ApproachSelector(10, 5, 11, true) == 11);
  // PickFlight thresholds (mirrors crow.c 80101650).
  CHECK(PickFlight(10, 0) == kAnimLand);
  CHECK(PickFlight(10, 3) == 0);   // wander
  CHECK(PickFlight(10, 9) == 1);   // approach
  CHECK(PickFlight(2, 0) == 1);    // few left: land=0, wander=0
  CHECK(PickFlight(4, 0) == kAnimLand);
  CHECK(PickFlight(4, 1) == 0);
  CHECK(PickFlight(4, 3) == 1);
  float vel = 0.0f;
  CHECK(DeadFallNextY(-100.0f, vel) == -90.0f);
  CHECK(vel == 12.0f);
  CHECK(DeadFallNextY(-5.0f, vel) == 0.0f);
  CHECK(DeadFallNextY(5.0f, vel) == 0.0f);
  if (failures) { std::printf("%d failures\n", failures); return 1; }
  std::printf("all crow checks passed\n");
  return 0;
}
