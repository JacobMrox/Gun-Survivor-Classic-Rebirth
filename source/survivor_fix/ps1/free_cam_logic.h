#pragma once
#include <cmath>

// Debug free camera maths (features/player.cpp). The camera keeps its own eye and direction; the PS1
// view is applied with them every physics frame instead of the player's. Pure, so the basis, the turn
// and the step are unit-tested without the game.
namespace ps1 {

constexpr float kFreeCamStep = 30.0f;        // world units per physics frame
constexpr float kFreeCamFastStep = 90.0f;    // with Shift held
constexpr float kFreeCamLookDistance = 300.0f;  // look-at distance: only the direction matters
constexpr float kFreeCamYawStep = 0.04f;     // radians per frame, NUMPAD 7 / 1

inline bool FreeCamNormalize(const float v[3], float out[3]) {
  const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (!(length > 1e-5f)) return false;
  out[0] = v[0] / length;
  out[1] = v[1] / length;
  out[2] = v[2] / length;
  return true;
}

// Turns the camera around the world Y axis, so it can be aimed where the player is not looking.
inline void FreeCamYaw(float dir[3], float radians) {
  const float c = std::cos(radians), s = std::sin(radians);
  const float x = dir[0] * c - dir[2] * s;
  const float z = dir[0] * s + dir[2] * c;
  dir[0] = x;
  dir[2] = z;
}

// Strafing basis: forward x worldUp, flattened (the camera's own up is the world's).
inline void FreeCamRight(const float forward[3], float right[3]) {
  right[0] = -forward[2];
  right[1] = 0.0f;
  right[2] = forward[0];
}

// ahead follows the view (including its pitch), side is level, lift is straight up. Step is applied to
// each so Shift only changes the distance, never the direction.
inline void FreeCamAdvance(float pos[3], const float forward[3], const float right[3], float ahead,
                           float side, float lift, float step) {
  pos[0] += (forward[0] * ahead + right[0] * side) * step;
  pos[1] += forward[1] * ahead * step + lift * step;
  pos[2] += (forward[2] * ahead + right[2] * side) * step;
}

}  // namespace ps1
