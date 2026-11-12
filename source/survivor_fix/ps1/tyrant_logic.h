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
// The wrapper never replaces the native enemy logic: it picks the clip and the clip speed from the
// measured travel, aims the native turn along a steer heading when a wall is in the way, and chooses
// the charge and sidestep clips. Collision, movement, attacks and damage stay native.
//
// WalkStep (80101404) adds three decisions on top of the clips, ported here:
//   - the stride and the charge are gated by the distance from the player (0xBB9 = 3001 for the
//     stride, 0x1771 = 6001 for the charge roll, `en_rand() % 100 < 0x19` and the player in front);
//   - the charge clip 0x11 and the double step (speed = -0x1A, shifted left while striding);
//   - the sidestep: with the player running past, close and in front, the tyrant swings to the side
//     the player is on (side 1: clip 5, side 2: clip 0x15). The PS1 sets TW_DODGED and clears it when
//     the swing ends, so one sidestep is used per run-past.
// Wall steering is the Steer state machine of the same function; it runs on the shared PS1 nav logic
// (ps1/nav_logic.h, NavStep) instead of being restated here.
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

// ---- WalkStep (80101404) -------------------------------------------------------------------------
constexpr float kTyrantStrideRange = 3001.0f;  // 0xBB9: stride (and reset of the charge roll) beyond
constexpr float kTyrantChargeRange = 6001.0f;  // 0x1771: charge roll inside
constexpr int kTyrantChargeOdds = 25;          // en_rand() % 100 < 0x19
constexpr float kTyrantWalkStep = 26.0f;       // speed = -0x1A per frame
constexpr float kTyrantStrideStep = 52.0f;     // speed <<= 1 while the parts' direction is 0x20
// 0043A364, the PC charge case: accumulate (0, 0, 40) once when the charge clip starts.
constexpr float kTyrantChargeImpulse = 40.0f;
constexpr int kTyrantChargeSelector = 0x11;
constexpr int kTyrantSwingLeft = 5;     // PlayerSide 1: ty_anim(e, 5, 0x40)
constexpr int kTyrantSwingRight = 0x15; // PlayerSide 2: ty_anim(e, 0x15, 0x40)
// Facing gate of the charge and the sidestep (the PS1 tests a cone; 60 degrees here).
constexpr float kTyrantFacingCosine = 0.5f;

constexpr bool TyrantFacing(float fx, float fz, float tx, float tz) {
  const float dot = fx * tx + fz * tz;
  const float squared = (fx * fx + fz * fz) * (tx * tx + tz * tz);
  return dot > 0.0f && dot * dot >= kTyrantFacingCosine * kTyrantFacingCosine * squared;
}

// Beyond the stride range the charge roll is rearmed (TW_CHARGED = 0).
constexpr bool TyrantResetsCharge(float distance) { return distance >= kTyrantStrideRange; }
constexpr bool TyrantStriding(float distance) { return distance >= kTyrantStrideRange; }
constexpr float TyrantStep(bool striding) { return striding ? kTyrantStrideStep : kTyrantWalkStep; }

// One charge per approach: inside 6001, a quarter of the rolls, with the player in front.
constexpr bool TyrantShouldCharge(bool charged, float distance, int roll, bool facing) {
  return !charged && distance < kTyrantChargeRange && roll < kTyrantChargeOdds && facing;
}

// PlayerSide of 80100FE8: 0 straight ahead, 1 and 2 the two sides of the line.
constexpr int TyrantSideFromLineSide(float side) { return side > 0.0f ? 1 : side < 0.0f ? 2 : 0; }
constexpr int TyrantSideSelector(int side) {
  return side == 1 ? kTyrantSwingLeft : side == 2 ? kTyrantSwingRight : 0;
}

// The sidestep of WalkStep: close, in front, and the player moving past (g_playerMoveFlags & 1).
constexpr bool TyrantCanSidestep(bool charging, float distance, bool facing, bool playerMoving, int side) {
  return !charging && side != 0 && distance < kTyrantStrideRange && facing && playerMoving;
}

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
