// Mr. X (PC types 22 and 23; PS1 tyrant, port/decomp/tyrant.c, 0x80105270).
//
// Both PC types share one case of the mapper 00436E40 (0x43A201), and that case already uses the PS1
// clip numbers: 0 idle, 1 walk, 2 punch, 5 swing, 0xB slam, 0x10 lunge, 0x11 charge, 0x13 aim. Two
// things it does not do like the PS1:
//
//   - the death request (0x0F/0x10) returns 8 about once in thirty times (rand % 0x1E < 3) and 9
//     otherwise. The PS1 always starts 9 (n_tyrant_OnHit: ty_anim(e, 9, 0x10)); 8 is the kneel that
//     never gets up, which is what the game shows as "the wrong death animation".
//   - the walk request (2/3/4, and 5-7 for the faster generic requests) always returns 1 with S+40 =
//     3.0 wherever the enemy logic wants the body to go. 3.0 is the PS1 real-time playback (the
//     zombies' 1x value is 2.93, config.h) and the PS1 tyrant covers 26 units per frame at 1x
//     (n_tyrant_WalkStep: speed = -0x1A, doubled while charging). With any other travel the feet do
//     not follow the ground: the body skates when the PC rushes it and the walk plays on the spot
//     when the PC holds it still.
//
// The wrapper only picks the clip and the clip speed from the measured travel; it never changes the
// movement itself, so the enemy logic, collision and attacks stay native.
#pragma once

namespace ps1 {

// 22 (0x16) and 23 (0x17) are the two Mr. X models; 24/25 have no model file in this install and take
// the mapper's default case.
constexpr bool IsTyrant(int type) { return type == 0x16 || type == 0x17; }

constexpr int kTyrantIdleSelector = 0;
constexpr int kTyrantWalkSelector = 1;
constexpr int kTyrantDeathSelector = 9;

// S+40 of the mapper's own idle and walk cases (0043A21D: 2.0, 0043A24C: 3.0).
constexpr float kTyrantIdleSpeed = 2.0f;
constexpr float kTyrantWalkSpeed = 3.0f;
// n_tyrant_WalkStep: speed = -0x1A, i.e. 26 units per frame at 1x.
constexpr float kTyrantStride = 26.0f;

// Hysteresis of the standstill (units of travel between two updates): enter below 3, leave above 8,
// so a body brushing a wall does not toggle the clip every frame.
constexpr float kTyrantStandEnter = 3.0f;
constexpr float kTyrantStandExit = 8.0f;

constexpr bool IsTyrantDeathRequest(int request) { return request == 0x0F || request == 0x10; }
// The PC requests 5-7 are the faster generic locomotion; the tyrant has no run clip.
constexpr bool IsTyrantWalkRequest(int request) { return request >= 2 && request <= 7; }

// True while the body is not travelling: the PS1 walks with the walk clip only when it moves.
constexpr bool TyrantStanding(bool standing, float travel) {
  return standing ? travel < kTyrantStandExit : travel < kTyrantStandEnter;
}

constexpr int TyrantWalkSelector(bool standing) {
  return standing ? kTyrantIdleSelector : kTyrantWalkSelector;
}

// S+40 for this update's travel: the clip plays as many times faster as the body travels faster than
// the PS1 stride at 1x, and as many times slower when it travels less. Bounded to a quarter and to
// three times so a teleport or a nudge cannot produce a still frame or a blur.
constexpr float TyrantWalkClipSpeed(float travel) {
  if (travel <= kTyrantStride * 0.25f) return kTyrantWalkSpeed * 0.25f;
  if (travel >= kTyrantStride * 3.0f) return kTyrantWalkSpeed * 3.0f;
  return kTyrantWalkSpeed * travel / kTyrantStride;
}

}  // namespace ps1
