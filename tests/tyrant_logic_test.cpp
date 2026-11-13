#include <cmath>
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
  // WalkStep: the stride and the charge roll are gated by the distance (3001 / 6001) and 25%.
  CHECK(!TyrantStriding(3000.0f));
  CHECK(TyrantStriding(3001.0f));
  CHECK(TyrantStep(false) == 26.0f);
  CHECK(TyrantStep(true) == 52.0f);
  CHECK(TyrantEntersStride(false, 3001.0f));
  CHECK(!TyrantEntersStride(true, 3001.0f));
  CHECK(!TyrantEntersStride(false, 3000.0f));
  CHECK(TyrantLeavesStride(3000.0f));
  CHECK(!TyrantLeavesStride(3001.0f));
  CHECK(TyrantShouldCharge(false, 5000.0f, 24, true));
  CHECK(!TyrantShouldCharge(false, 5000.0f, 25, true));
  CHECK(!TyrantShouldCharge(true, 5000.0f, 0, true));
  CHECK(!TyrantShouldCharge(false, 6001.0f, 0, true));
  CHECK(!TyrantShouldCharge(false, 5000.0f, 0, false));
  // The aim stop: far, in front, a shot just fired, no aim running and off cooldown.
  CHECK(TyrantShouldAim(false, 3001.0f, true, true, true));
  CHECK(!TyrantShouldAim(true, 3001.0f, true, true, true));
  CHECK(!TyrantShouldAim(false, 3000.0f, true, true, true));
  CHECK(!TyrantShouldAim(false, 4000.0f, false, true, true));
  CHECK(!TyrantShouldAim(false, 4000.0f, true, false, true));
  CHECK(!TyrantShouldAim(false, 4000.0f, true, true, false));
  CHECK(kTyrantAimSelector == 0x13);
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
  // en_turn_to(0x30): about 4.2 degrees per update.
  const float turnDegrees = kTyrantTurnStep * 180.0f / 3.14159265f;
  CHECK(turnDegrees > 4.1f && turnDegrees < 4.3f);
  if (failures) return 1;
  // Clip rate follows the ground actually covered: a full step keeps the base, a slide is scaled
  // down, a pinned body drops to the floor, and it never runs above the authored rate.
  CHECK(std::fabs(TyrantClipRate(1.0f, 26.0f, 26.0f) - 1.0f) < 1e-5f);
  CHECK(std::fabs(TyrantClipRate(1.0f, 52.0f, 26.0f) - 0.5f) < 1e-5f);
  CHECK(std::fabs(TyrantClipRate(2.0f, 52.0f, 3.6f) - 0.5f) < 1e-5f);
  CHECK(std::fabs(TyrantClipRate(1.0f, 52.0f, 80.0f) - 1.0f) < 1e-5f);
  CHECK(std::fabs(TyrantClipRate(1.0f, 0.0f, 0.0f) - 0.25f) < 1e-5f);
  CHECK(std::fabs(TyrantClipRate(2.0f, 52.0f, 52.0f) - 2.0f) < 1e-5f);
  // A turn that cannot finish is abandoned so the body takes a step and slides along the geometry.
  CHECK(!TyrantTurnGivesUp(0));
  CHECK(!TyrantTurnGivesUp(kTyrantTurnGiveUp));
  CHECK(TyrantTurnGivesUp(kTyrantTurnGiveUp + 1));
  CHECK(kTyrantEscapeUpdates > kTyrantTurnGiveUp);
  std::puts("PASS: Mr. X walk step, charge, aim, sidestep and death rules");
  return 0;
}
