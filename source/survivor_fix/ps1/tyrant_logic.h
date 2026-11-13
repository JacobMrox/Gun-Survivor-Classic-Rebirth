// Mr. X (PC types 22 and 23; PS1 tyrant, port/decomp/tyrant.c, 0x80105270).
//
// Both PC types share one case of the mapper 00436E40 (0x43A201), and that case already uses the PS1
// clip numbers: 0 idle, 1 walk, 2 punch, 5 swing, 0xB slam, 0x10 lunge, 0x11 charge, 0x13 aim, 0x15 the
// other side's swing. What it does not do like the PS1:
//
//   - the death request (0x0F/0x10) returns 8 about once in thirty times (rand % 0x1E < 3) and 9
//     otherwise. The PS1 always starts 9 (n_tyrant_OnHit: ty_anim(e, 9, 0x10)); 8 is the kneel that
//     never gets up.
//   - the walk request always returns 1 at S+40 = 3.0 and lets the enemy logic pick the speed, so the
//     feet do not follow the ground. 3.0 is the PS1 real-time playback (the zombies' 1x value is 2.93)
//     and the PS1 tyrant covers 26 units per frame at 1x (n_tyrant_WalkStep: speed = -0x1A), so the
//     body is moved 26 units per update (52 while charging) and the clip stays at the authored rate.
//   - WalkStep has three decisions the PC logic does not make: a charge, a sidestep to the player's
//     side, and the aim stop (0x13) when the player fires. They are modelled below.
//
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
constexpr int kTyrantChargeSelector = 0x11;
constexpr int kTyrantAimSelector = 0x13;
constexpr int kTyrantSwingLeft = 5;      // PlayerSide 1: ty_anim(e, 5, 0x40)
constexpr int kTyrantSwingRight = 0x15;  // PlayerSide 2: ty_anim(e, 0x15, 0x40)

// S+40 of the mapper's idle case (0043A21D) is 2.0. The walk case writes 3.0, but that is not this
// model's 1x rate: measured in game (2026-11-13, Tyrant trace) the body covers exactly 26 units per
// update while the legs cycle at more than twice the cadence that stride needs. The PS1 walk of
// tyrant.c is a slow stalk, so the walk is played at half the mapper's value, which brings the feet
// back onto the 26 unit step; the idle keeps the mapper's 2.0.
// The mapper writes 3.0 for the walk clip, but the port moves him 26 units per update: at that step the
// mapper's rate cycles the legs twice for every stride, which is the "legs double the torso" report. The
// scale is applied where the clip is handed to each track (00412B90), after the mapper, so it sticks.
constexpr float kTyrantWalkClipScale = 0.5f;
constexpr float kTyrantChargeClipScale = 1.0f;  // the charge step is doubled, so its clip stays as authored
constexpr float kTyrantIdleSpeed = 2.0f;
constexpr float kTyrantWalkSpeed = 1.0f;
// The charge covers 52 units per update, twice the walk, so its clip runs at twice the walk rate.
constexpr float kTyrantChargeSpeed = 2.0f;
// The clip rate follows the ground the body actually covered in the last update, not the step it asked
// for: a charge pinned on a wall, or a walk sliding along one, must not keep pumping the legs at full
// stride while the body covers a handful of units (the "legs faster than he moves" complaint). The
// scale is clamped so a nudge never stops the legs and a slide can never outrun the authored rate.
constexpr float TyrantClipRate(float base, float intendedStep, float travelled) {
  if (!(intendedStep > 1.0f) || !(travelled > 0.0f)) return base * 0.25f;
  const float ratio = travelled / intendedStep;
  if (!(ratio > 0.0f)) return base * 0.25f;
  const float clamped = ratio > 1.0f ? 1.0f : (ratio < 0.25f ? 0.25f : ratio);
  return base * clamped;
}

// ---- WalkStep (80101404) -------------------------------------------------------------------------
constexpr float kTyrantStrideRange = 3001.0f;  // 0xBB9: the stride band and the charge rearm
constexpr float kTyrantChargeRange = 6001.0f;  // 0x1771: the charge roll is only made inside
constexpr int kTyrantChargeOdds = 25;          // en_rand() % 100 < 0x19
constexpr float kTyrantWalkStep = 26.0f;       // speed = -0x1A per frame
constexpr float kTyrantStrideStep = 52.0f;     // speed <<= 1 in the stride band
// 0043A364, the PC charge case: accumulate (0, 0, 40) once when the charge clip starts.
constexpr float kTyrantChargeImpulse = 40.0f;
// A charge lasts at least this many updates, so a short charge clip is still a burst of steps, and at
// most this many: the charge clip is reapplied every update, which keeps its completion flag from
// rising, so the port ends the charge on its own counter instead of waiting for it.
constexpr int kTyrantChargeMinimum = 24;
constexpr int kTyrantChargeUpdates = 45;
// Charging into a wall for this many updates ends the charge instead of pinning him there.
constexpr int kTyrantChargeBlocked = 6;
// en_turn_to(0x30): at most 0x30 angle units (4.2 degrees) per frame.
constexpr float kTyrantTurnStep = 0x30 * 2.0f * 3.14159265f / 4096.0f;

// The aim stop: the player fired a heavy weapon (`PlayerFiring` returns 0), the tyrant is far and the
// player in front; clip 0x13, then walking again. The PC has no weapon type, so any shot counts.
constexpr int kTyrantAimUpdates = 12;
constexpr int kTyrantAimCooldown = 40;
constexpr int kTyrantAimShotWindow = 3;  // updates between the shot and the reaction

// Facing gate of the charge, the sidestep and the aim (the PS1 tests a cone; 60 degrees here).
constexpr float kTyrantFacingCosine = 0.5f;

// Hysteresis of the standstill (units of travel between two updates).
constexpr float kTyrantStandEnter = 3.0f;
constexpr float kTyrantStandExit = 8.0f;

constexpr bool IsTyrantDeathRequest(int request) { return request == 0x0F || request == 0x10; }
// The PC requests 5-7 are the faster generic locomotion; the tyrant has no run clip.
constexpr bool IsTyrantWalkRequest(int request) { return request >= 2 && request <= 7; }

constexpr bool TyrantFacing(float fx, float fz, float tx, float tz) {
  const float dot = fx * tx + fz * tz;
  const float squared = (fx * fx + fz * fz) * (tx * tx + tz * tz);
  return dot > 0.0f && dot * dot >= kTyrantFacingCosine * kTyrantFacingCosine * squared;
}

// The far branch of WalkStep switches the parts to the stride direction and rearms the charge roll
// once; every later update may roll again, so a charge can start well before the player is close.
constexpr bool TyrantEntersStride(bool strideMode, float distance) {
  return distance >= kTyrantStrideRange && !strideMode;
}
constexpr bool TyrantLeavesStride(float distance) { return distance < kTyrantStrideRange; }
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

// The aim stop of WalkStep: far away, player in front, a shot just fired and no aim running.
constexpr bool TyrantShouldAim(bool aiming, float distance, bool facing, bool shotRecently, bool ready) {
  return !aiming && ready && shotRecently && facing && distance >= kTyrantStrideRange;
}

// True while the body is not travelling: the PS1 walks with the walk clip only when it moves.
constexpr bool TyrantStanding(bool standing, float travel) {
  return standing ? travel < kTyrantStandExit : travel < kTyrantStandEnter;
}

constexpr int TyrantWalkSelector(bool standing) {
  return standing ? kTyrantIdleSelector : kTyrantWalkSelector;
}

}  // namespace ps1
