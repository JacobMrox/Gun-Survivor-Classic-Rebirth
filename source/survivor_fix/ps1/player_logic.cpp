#include "player_logic.h"

namespace ps1 {

int StepTurn(PlayerTurn& t, int input) {
  if (input != 0) {
    // ((64 * 7) / 10) * counter, then / 10 truncating towards zero (multiplication by 0x66666667).
    const int v = kTurnMax * t.counter;
    t.rate = (input > 0 ? v : -v) / 10;
    if (t.counter < kTurnCounterMax) ++t.counter;
  } else if (t.rate > kTurnRelease) {
    t.rate = kTurnRelease;
  } else if (t.rate < -kTurnRelease) {
    t.rate = -kTurnRelease;
  } else if (t.rate > 0) {
    --t.rate;
  } else if (t.rate < 0) {
    ++t.rate;
  } else {
    t.counter = 0;
  }
  if ((t.previous > 0 && t.rate < 0) || (t.previous < 0 && t.rate > 0)) {
    t.rate = 0;
    t.counter = 0;
  }
  t.previous = t.rate;
  return t.rate;
}

}  // namespace ps1
