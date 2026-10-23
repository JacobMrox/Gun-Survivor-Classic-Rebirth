// Which PC enemy types are zombies, and their walk clips.
#pragma once

namespace ps1 {

// PC 00436E40 shared mapper; PS1 800A2234/800A3B80 -> 80105180/801047B4.
// These are PC native model IDs, not raw PS1 resource IDs. 03/07 have no
// corresponding case in this PC mapper and must never be admitted by a range.
constexpr bool IsPcZombie(int type) {
  switch (type) {
    case 0: case 1: case 2: case 4: case 5: case 6: case 8: case 9: case 10:
      return true;
  }
  return false;
}

constexpr bool SpecialZombieWalk(int type) { return type == 9 || type == 10; }

// The highest selector required by the shared phases is 0x25; the special walks end at 0x29.
// ENEM04/05/06 have 38 sequences; the other variants have 42.
constexpr int RequiredZombieSequences(int type) { return SpecialZombieWalk(type) ? 42 : 38; }

constexpr bool IsZombieWalk(int type, int selector) {
  return IsPcZombie(type) && (SpecialZombieWalk(type) ? selector >= 0x26 && selector <= 0x29
                                                      : selector >= 1 && selector <= 4);
}

// 80102828: arms forward (clip 4) below this distance, with rand < kArmsRng once.
constexpr float kArmsDistance = 0x5DD;
constexpr int kArmsRng = 0x3F;

constexpr bool IsPs1WalkClip(int selector) {
  return (selector >= 1 && selector <= 5) || (selector >= 0x26 && selector <= 0x29);
}

}  // namespace ps1
