#include "death_logic.h"

namespace ps1 {

namespace {

DeathPlan Direct(int clip) { return {{clip, 0}, 1, kDeathBlendDirect}; }
DeathPlan State(int clip) { return {{clip, 0}, 1, kDeathBlendState}; }

}  // namespace

DeathPlan PickDeath(int currentClip, DeathPart part, bool behind, int armRoll, int sideRoll) {
  switch (currentClip - 8) {
    case 0x00: case 0x0A: case 0x0B: case 0x1D: case 0x27:  // 08, 0x12, 0x13, 0x25, 0x2F: 0x8009
      return Direct(9);
    case 0x06: case 0x07: case 0x10:  // 0E, 0F, 0x18
      return State(9);
    case 0x01: case 0x0D:  // 09, 0x15: the clip goes on
      return {{0, 0}, 0, kDeathBlendState};
    case 0x04: case 0x08: case 0x09: case 0x0F:  // 0C, 0x10, 0x11, 0x17
      return State(0x0D);
    case 0x1B: case 0x1C:  // 0x23, 0x24: 0x800D
      return Direct(0x0D);
    case 0x05:  // 0D
      return State(0x1A);
    default:
      break;
  }
  if (part == DeathPart::Head) return State(behind ? 0x0C : 0x08);  // type 1, sectors 2-5 behind
  if (part == DeathPart::Arm && armRoll <= 0x1F) {
    // Types 3 and 4 with rand <= 0x1F: 06 or 0A by sector, 80% / 20%.
    const bool first = sideRoll < 0xCC;
    return Direct(behind == first ? 0x0A : 0x06);
  }
  return {{0x0B, 0x0C}, 2, kDeathBlendDirect};  // 0x800B; 0x0B goes on into 0x0C
}

bool DeathLoops(int clip) { return clip == 0x12 || clip == 0x13; }

int DeathSecond(int clip, int roll) {
  if (clip == 0x0C || clip == 0x13) return roll <= 0x7E ? 0x13 : -1;
  if (clip == 0x08 || clip == 0x12) return roll < 0x7F ? 0x12 : -1;
  return -1;
}

}  // namespace ps1
