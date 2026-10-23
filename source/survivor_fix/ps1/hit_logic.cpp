#include "hit_logic.h"

namespace ps1 {

HitKind ClassifyHit(int clip, bool knockbackBit) {
  if (knockbackBit) return HitKind::Knockback;
  switch (clip) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x14: case 0x1B: case 0x1C: case 0x1F:
    case 0x26: case 0x27: case 0x28: case 0x29:
      return HitKind::Table;
    case 0x06: case 0x08: case 0x0C: case 0x15:
      return HitKind::Keep;
    default:
      return HitKind::None;  // 7, 0xA, 0xB and the others (0xD -> 0x19 is not used by the PC zombie)
  }
}

int HitTableRow(int rngByte) {
  const int r = rngByte & 0xFF;
  if (r == 0) return kHitNoReaction;
  if (r <= 0x14) return 0;
  if (r <= 0x19) return 2;
  if (r < 0x1F) return 3;
  return kHitNoReaction;
}

int HitTableClip(int row, bool limb, bool behind) {
  static const int kTable[16] = {0x15, 0x15, 0x15, 0x15, 0x08, 0x0C, 0x0C, 0x08,
                                 0x07, 0x0B, 0x0B, 0x07, 0x06, 0x0A, 0x0A, 0x06};
  if (row < 0 || row > 3) return -1;
  return kTable[row * 4 + (limb ? 2 : 0) + (behind ? 1 : 0)];
}

int HitSlideBase(float distance) {
  if (distance > 10000.0f) return 200;
  return distance > 5000.0f ? 500 : 1000;
}

int HitSlide(int clip, int slideBase, int current) {
  switch (clip) {
    case 0x06: case 0x0A: return current + slideBase / 2;
    case 0x07: case 0x0B: return current + slideBase;
    case 0x08: case 0x0C: return current + slideBase * 6 / 5;
    case 0x20: return 500;
    default: return current;
  }
}

bool HitPushes(int clip) {
  switch (clip) {
    case 0x06: case 0x0A: case 0x07: case 0x0B: case 0x08: case 0x0C: case 0x20:
      return true;
    default:
      return false;
  }
}

}  // namespace ps1
