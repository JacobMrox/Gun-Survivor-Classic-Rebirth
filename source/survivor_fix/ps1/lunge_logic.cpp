#include "lunge_logic.h"

namespace ps1 {

void ApplyBandMask(LungeBits& b, int band) {
  switch (band) {
    case 1: b.bits &= ~(kLungeBand2Bit | kLungeBand3Bit); break;  // ~0x17
    case 2: b.bits &= ~kLungeBand3Bit; break;                     // ~0x1B, keeps 4
    case 3: b.bits &= ~kLungeBand2Bit; break;                     // ~0x1D, keeps 2
    case 4: b.bits &= ~(kLungeBand2Bit | kLungeBand3Bit); break;  // ~0x1E
    default: break;
  }
}

bool ShouldStartLunge(LungeBits& b, int band, int rngByte, bool zombieSectorOk) {
  const int r = rngByte & 0xFF;
  bool roll = false;
  if (band == 2) {
    if (b.bits & kLungeBand2Bit) return false;
    b.bits |= kLungeBand2Bit;
    roll = r < kLungeBand2Rng;
  } else if (band == 3) {
    if (b.bits & kLungeBand3Bit) return false;
    b.bits |= kLungeBand3Bit;
    roll = r < kLungeBand3Rng;
  } else {
    return false;
  }
  return roll && zombieSectorOk;
}

bool LungeGrabs(const LungeBits& b, float distance, bool inPlayerCone, bool gateOk) {
  return distance < kLungeGrabDistance && inPlayerCone && (b.bits & kLungeBand3Bit) && gateOk;
}

int LungeSpeed(int walkSpeed, int factor) {
  const int v = walkSpeed * factor;
  return v > 127 ? 127 : v;
}

GrabSlide StartGrabSlide(int offsetUnits) {
  GrabSlide s;
  s.offset = offsetUnits;
  s.active = true;
  return s;
}

int StepGrabSlide(GrabSlide& s) {
  if (!s.active) return 0;
  const int current = s.offset;
  if (current == 0) {
    s.active = false;
    return 0;
  }
  // Substates 3/4: position with the current offset, then |offset| -= 0x37 (minimum 0).
  if (current > 0) {
    s.offset = current - kLungeSlideStep < 1 ? 0 : current - kLungeSlideStep;
  } else {
    s.offset = current + kLungeSlideStep > -1 ? 0 : current + kLungeSlideStep;
  }
  return current;
}

}  // namespace ps1
