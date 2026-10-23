#include "speed_logic.h"

#include <cmath>
#include <limits>

namespace ps1 {

int PickWalkSpeed(int rngByte) {
  const int r = rngByte & 0xFF;
  int i = 0;
  while (i < 7 && r > kWalkSpeedOdds[i] * 0x33 / 0x14) ++i;
  return kWalkSpeeds[i];
}

int WalkSpeedForDistance(int base, int distance) {
  // The PS1 works with a negative base and divides truncating towards zero: on the
  // magnitude it is the same integer division.
  int speed = base;
  if (distance > 5000) {
    if (distance < 10001) {
      speed = speed * 6 / 5;
    } else if (distance < 15001) {
      speed = speed * 3 / 2;
    } else if (distance < 20001) {
      speed = speed * 7 / 4;
    } else {
      speed = speed * 2;
    }
  }
  return speed > kMaxWalkSpeed ? kMaxWalkSpeed : speed;
}

bool RoundWalkComponent(float component, int32_t& output) {
  // 8008286C: arithmetic >>8, then +1 if the saved low byte >=0x80.
  // For q/256 this is floor(q/256 + 0.5), not truncation or lround.
  const double rounded = std::floor(double(component) + 0.5);
  if (!std::isfinite(rounded) ||
      rounded < (std::numeric_limits<int32_t>::min)() ||
      rounded > (std::numeric_limits<int32_t>::max)()) return false;
  output = static_cast<int32_t>(rounded);
  return true;
}

int WalkAnimMultiplier(int base) {
  int diff = base - kWalkSpeeds[0];
  if (diff < 0) diff = -diff;
  int n = 0;
  for (; diff > 0; diff -= 4) ++n;  // like the loop of 80104E78
  return n + 1;
}

void WalkSpeedLifetime::Initialize(std::uintptr_t actor, int rngByte) {
  bases_[actor] = PickWalkSpeed(rngByte);
}

void WalkSpeedLifetime::Forget(std::uintptr_t actor) {
  bases_.erase(actor);
}

int WalkSpeedLifetime::Base(std::uintptr_t actor) const {
  const auto it = bases_.find(actor);
  return it == bases_.end() ? 0 : it->second;
}

}  // namespace ps1
