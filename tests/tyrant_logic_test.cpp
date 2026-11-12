#include <cstdio>
#include "../source/survivor_fix/ps1/tyrant_logic.h"

using namespace ps1;
int main() {
  int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
  CHECK(IsTyrant(0x16));
  CHECK(IsTyrant(0x17));
  CHECK(!IsTyrant(0x15));
  CHECK(!IsTyrant(0x18));
  CHECK(!IsTyrant(0x18 + 1));
  // Death requests 0x0F/0x10 keep the PS1 clip 9, whatever the PC roll chose.
  CHECK(IsTyrantDeathRequest(0x0F));
  CHECK(IsTyrantDeathRequest(0x10));
  CHECK(!IsTyrantDeathRequest(0x11));
  CHECK(kTyrantDeathSelector == 9);
  // Walk requests: the generic 2-7 all play the walk clip 1.
  for (int request = 2; request <= 7; ++request) CHECK(IsTyrantWalkRequest(request));
  CHECK(!IsTyrantWalkRequest(0));
  CHECK(!IsTyrantWalkRequest(1));
  CHECK(!IsTyrantWalkRequest(8));
  CHECK(!IsTyrantWalkRequest(9));
  CHECK(!IsTyrantWalkRequest(0x0B));
  // Standstill hysteresis: a body that does not travel goes idle and walks again only above 8.
  CHECK(TyrantStanding(false, 0.0f));
  CHECK(TyrantStanding(false, 2.9f));
  CHECK(!TyrantStanding(false, 3.0f));
  CHECK(TyrantStanding(true, 7.9f));
  CHECK(!TyrantStanding(true, 8.0f));
  CHECK(TyrantWalkSelector(true) == 0);
  CHECK(TyrantWalkSelector(false) == 1);
  CHECK(kTyrantIdleSelector == 0);
  // Clip speed follows the travel: 26 units is the PS1 1x cadence (S+40 = 3.0), half is half, and
  // both extremes are bounded.
  CHECK(TyrantWalkClipSpeed(26.0f) == 3.0f);
  CHECK(TyrantWalkClipSpeed(13.0f) == 1.5f);
  CHECK(TyrantWalkClipSpeed(52.0f) == 6.0f);
  CHECK(TyrantWalkClipSpeed(0.0f) == 0.75f);
  CHECK(TyrantWalkClipSpeed(6.0f) == 0.75f);
  CHECK(TyrantWalkClipSpeed(200.0f) == 9.0f);
  CHECK(kTyrantIdleSpeed == 2.0f);
  // WalkStep: the stride and the charge roll are gated by the distance (3001 / 6001) and 25%.
  CHECK(!TyrantStriding(3000.0f));
  CHECK(TyrantStriding(3001.0f));
  CHECK(TyrantStep(false) == 26.0f);
  CHECK(TyrantStep(true) == 52.0f);
  CHECK(TyrantResetsCharge(3001.0f));
  CHECK(!TyrantResetsCharge(3000.0f));
  CHECK(TyrantShouldCharge(false, 5000.0f, 24, true));
  CHECK(!TyrantShouldCharge(false, 5000.0f, 25, true));
  CHECK(!TyrantShouldCharge(true, 5000.0f, 0, true));
  CHECK(!TyrantShouldCharge(false, 6001.0f, 0, true));
  CHECK(!TyrantShouldCharge(false, 5000.0f, 0, false));
  // Sidestep: the sign of the line side picks the swing, one per run-past.
  CHECK(TyrantSideFromLineSide(1.0f) == 1);
  CHECK(TyrantSideFromLineSide(-1.0f) == 2);
  CHECK(TyrantSideFromLineSide(0.0f) == 0);
  CHECK(TyrantSideSelector(1) == 5);
  CHECK(TyrantSideSelector(2) == 0x15);
  CHECK(TyrantSideSelector(0) == 0);
  CHECK(TyrantCanSidestep(false, 2000.0f, true, true, 1));
  CHECK(TyrantCanSidestep(false, 2000.0f, true, true, 2));
  CHECK(!TyrantCanSidestep(true, 2000.0f, true, true, 1));
  CHECK(!TyrantCanSidestep(false, 3001.0f, true, true, 1));
  CHECK(!TyrantCanSidestep(false, 2000.0f, false, true, 1));
  CHECK(!TyrantCanSidestep(false, 2000.0f, true, false, 1));
  CHECK(!TyrantCanSidestep(false, 2000.0f, true, true, 0));
  // Facing gate: 60 degrees half angle.
  CHECK(TyrantFacing(0.0f, 1.0f, 0.0f, 10.0f));
  CHECK(TyrantFacing(0.0f, 1.0f, 5.0f, 10.0f));
  CHECK(!TyrantFacing(0.0f, 1.0f, 0.0f, -10.0f));
  CHECK(!TyrantFacing(0.0f, 1.0f, 10.0f, 1.0f));
  CHECK(!TyrantFacing(0.0f, 0.0f, 1.0f, 0.0f));
  if (failures) return 1;
  std::puts("PASS: Mr. X walk, standstill and death clip rules");
  return 0;
}
