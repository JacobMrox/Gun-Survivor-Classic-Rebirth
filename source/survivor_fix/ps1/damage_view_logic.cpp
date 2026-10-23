#include "damage_view_logic.h"

namespace ps1 {

void StepBiteJolt(BiteJolt& j) {
  if (j.done) return;
  if (j.phase > 3) {  // step 5
    j.yawOff = 0;
    j.lookY = kViewLookY;
    j.done = true;
    return;
  }
  // Step 1: 800B3EF8 -= 0xFA, minimum -1000.
  j.lookY -= 250;
  if (j.lookY < -1000) j.lookY = -1000;
  if (j.phase == 3) {  // step 2 with 800BB074 >= 3: on to step 5, 800A865E as it was
    ++j.phase;
    ++j.frame;
    return;
  }
  // Step 2 with 800BB078 = -50: even 865E += -50 (below 100 -> 100, phase + 1), odd 865E -= -50
  // (above 300 -> 300, phase + 1).
  if ((j.phase & 1) == 0) {
    j.yawOff -= 50;
    if (j.yawOff < 100) {
      j.yawOff = 100;
      ++j.phase;
    }
  } else {
    j.yawOff += 50;
    if (j.yawOff > 300) {
      j.yawOff = 300;
      ++j.phase;
    }
  }
  ++j.frame;
}

}  // namespace ps1
