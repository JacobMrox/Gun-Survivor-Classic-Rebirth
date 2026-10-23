#include "bite_logic.h"

#include <cmath>

namespace ps1 {

bool InBiteCone(Vec2 player, Vec2 playerForward, Vec2 zombie) {
  const float dx = zombie.x - player.x, dz = zombie.z - player.z;
  const float len = std::sqrt(dx * dx + dz * dz);
  const float flen = std::sqrt(playerForward.x * playerForward.x + playerForward.z * playerForward.z);
  if (len <= 0.0f || flen <= 0.0f) return true;  // overlapping: counts as inside
  const float cosine = (dx * playerForward.x + dz * playerForward.z) / (len * flen);
  return cosine >= std::cos(kBiteConeHalfAngle);
}

bool UpdateBiteCounter(BiteCounter& c, float distance, bool inCone) {
  if (distance > kBiteRange) return false;
  c.frames = inCone ? c.frames + 1 : 0;
  return c.frames >= kBiteFrames;
}

Vec2 PlayerPush(Vec2 player, Vec2 enemy, float dy, int enemyRadius) {
  const int r = kPlayerPushRadius + enemyRadius;
  const int dx = static_cast<int>(player.x - enemy.x);
  const int dz = static_cast<int>(player.z - enemy.z);
  // Square pre-check of 80081FF0 (unsigned): |dx| <= r and |dz| <= r.
  if (dx < -r || dx > r || dz < -r || dz > r) return {0, 0};
  const int dist = static_cast<int>(std::sqrt(static_cast<float>(dx * dx + dz * dz)));
  const int depth = r - dist;
  if (depth <= 0 || dy <= -kPushHeight || dy >= kPushHeight) return {0, 0};
  return {static_cast<float>(dx * depth / (dist + 1)), static_cast<float>(dz * depth / (dist + 1))};
}

bool BackGrabInRange(float distance, bool inCone) {
  return distance <= kBiteRange && !inCone;
}

GrabTurn StartGrabTurn(int angleUnits, int step) {
  GrabTurn t;
  t.sign = angleUnits < 0 ? -1 : 1;
  t.step = step;
  const int a = angleUnits < 0 ? -angleUnits : angleUnits;
  t.frames = a / step;
  t.rest = a % step;
  if (t.frames != 0 && t.rest < 6) t.rest = 6;
  if (t.rest < 12) t.rest = 0;
  t.active = true;
  return t;
}

int StepGrabTurn(GrabTurn& t) {
  if (!t.active) return 0;
  if (t.frames > 0) {
    --t.frames;
    return t.sign * t.step;
  }
  if (t.rest >= 7) {
    const int step = t.rest - 6;
    t.rest = 6;
    return t.sign * step;
  }
  if (t.rest == 0) {
    t.active = false;
    return 0;
  }
  const int step = t.rest--;
  return t.sign * step;
}

ViewTurn StartViewTurn(int yawUnits, int pitchUnits, int yawStep, int pitchStepMax) {
  ViewTurn v;
  v.yaw = StartGrabTurn(yawUnits, yawStep);
  const int yawAbs = yawUnits < 0 ? -yawUnits : yawUnits;
  const int pitchAbs = pitchUnits < 0 ? -pitchUnits : pitchUnits;
  const int pitchFrames = pitchAbs / pitchStepMax;
  if (v.yaw.frames < pitchFrames) {
    v.yaw.step = yawAbs / pitchFrames;
    v.yaw.rest = 0;
    v.yaw.frames = pitchFrames;
    v.pitchStep = pitchUnits < 0 ? -pitchStepMax : pitchStepMax;
  } else if (yawUnits == 0 || v.yaw.frames == 0) {
    v.pitchStep = 0;
  } else {
    v.pitchStep = pitchUnits / v.yaw.frames;
  }
  return v;
}

void StepViewTurn(ViewTurn& v, int& yawUnits, int& pitchUnits) {
  pitchUnits = v.yaw.frames > 0 ? v.pitchStep : 0;
  yawUnits = StepGrabTurn(v.yaw);
}

TurnResult TurnToward(Vec2 forward, Vec2 target, float maxStep, float doneAngle) {
  const float a = std::atan2(forward.x, forward.z);
  const float b = std::atan2(target.x, target.z);
  const float kPi = 3.14159265f;
  float diff = b - a;
  while (diff > kPi) diff -= 2.0f * kPi;
  while (diff < -kPi) diff += 2.0f * kPi;
  const bool done = std::fabs(diff) <= maxStep || std::fabs(diff) <= doneAngle;
  const float step = std::fabs(diff) <= maxStep ? diff : (diff > 0 ? maxStep : -maxStep);
  const float c = a + step;
  return {{std::sin(c), std::cos(c)}, done};
}

}  // namespace ps1
