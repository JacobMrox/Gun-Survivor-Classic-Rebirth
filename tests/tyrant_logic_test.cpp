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
  if (failures) return 1;
  std::puts("PASS: Mr. X walk, standstill and death clip rules");
  return 0;
}
