#include "backfall_logic.h"

namespace ps1 {

bool ShouldStartBackFall(int rngByte) { return (rngByte & 0xFF) < kBackFallRngThreshold; }

bool IsBackFallCheckFrame(int cursor, int start, int frame) { return start >= 0 && cursor == start + frame; }

void StartBackPush(BackPush& p, float magnitude) {
  p.strength = kBackPushStart;
  p.magnitude = magnitude;
  p.delay = 0;
  p.zeroLast = false;
}

void StartKnockbackPush(BackPush& p, float magnitude) {
  StartBackPush(p, magnitude);
  p.delay = 1;
  p.zeroLast = true;
}

float NextBackPush(BackPush& p) {
  if (p.strength <= 0) return 0.0f;
  if (p.delay > 0) {
    --p.delay;
    return 0.0f;
  }
  float push = p.magnitude * static_cast<float>(p.strength);
  p.strength -= kBackPushStep;
  if (p.strength < 1) {
    p.strength = 0;
    if (p.zeroLast) push = 0.0f;
  }
  return push;
}

}  // namespace ps1
