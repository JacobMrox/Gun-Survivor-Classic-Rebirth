// Partial, platform-neutral Crow decision model. Not yet wired beyond anim mapping.
// Behavior reference: RESPeCt port/decomp/crow.c (GPL-3.0), ENEMPROG 52 (slot B),
// crow_Update 0x80103C38, state 1 crow_StateActive 0x80103E30, actions 3-13.
// PC type hypothesis: 12 (ENEMPROG 52 - 40, adjacent to Cerberus PC type 11 =
// ENEMPROG 51; ENEM12.NAB present and small; to be verified live before full AI).
#pragma once

namespace ps1::crow {

// PC native model ID for the crow (see above; verify via 00436E40 dispatch).
constexpr int kCrowType = 12;

// PS1 crow animation ids from crow.c (anim_set / FN_EN_SET_ACTION).
constexpr int kAnimLand = 3;
constexpr int kAnimFly = 4;
constexpr int kAnimPeck = 5;
constexpr int kAnimSwoop = 6;
constexpr int kAnimPerchHop = 7;
constexpr int kAnimTakeoff = 8;
constexpr int kAnimDeathFeathers = 9;
constexpr int kAnimLandFinal = 10;
constexpr int kAnimTwitch = 11;
constexpr int kAnimDeadFall = 13;

// PC animation requests that mean locomotion (matches IsLocomotion 2-4 and
// the native 2-7 footstep range used by the Cerberus adapter).
constexpr bool IsLocomotionRequest(int request) {
  return request >= 2 && request <= 7;
}

// PickFlight (80101650): roll = (CW_COUNT + rand) % 10. Fewer crows left
// means land more often. Mirrors the PS1 thresholds.
inline int PickFlight(int crowCount, int roll) {
  int land, wander;
  if (crowCount > 0 && crowCount < 3) { land = 0; wander = 0; }
  else if (crowCount > 0 && crowCount < 5) { land = 1; wander = 2; }
  else { land = 1; wander = 4; }
  if (roll < land) return kAnimLand;
  if (roll < wander) return 0;  // CW_FLIGHT 0 (wander)
  return 1;  // CW_FLIGHT 1 (approach)
}

// Animation correction for the PC mapper: crows in flight must keep the
// flying clip (4) while locomotion is requested. The broken PC port plays
// perched/idle clips while flying. Attacks (5/6), land/takeoff (3/8) are left
// to native handling; once dead the crow must take the dead-fall clip (13,
// DeadFallStep) so it falls to the floor instead of looping the twitch (11)
// or freezing mid-air. Any death request maps to 13.
constexpr int ApproachSelector(int type, int request, int native, bool alive) {
  if (type != kCrowType) return native;
  if (!alive) return kAnimDeadFall;
  return IsLocomotionRequest(request) && native != kAnimFly
      ? kAnimFly
      : native;
}

// Dead fall (crow.c 80103898 DeadFallStep, action 13): fall to the floor with
// accelerating velocity (starts 0xA, +2 per frame), then anim 11. PC Y is
// negative aloft (PS1 scale), 0 at the floor — same as zombie support logs.
constexpr float kDeadFallStart = 10.0f;
constexpr float kDeadFallAccel = 2.0f;
inline float DeadFallNextY(float y, float& vel) {
  if (y >= 0.0f) { vel = 0.0f; return 0.0f; }
  if (vel <= 0.0f) vel = kDeadFallStart;
  y += vel;
  vel += kDeadFallAccel;
  if (y > 0.0f) y = 0.0f;
  if (y == 0.0f) vel = 0.0f;
  return y;
}

// Wing flap (FlyStep 80101C58: CW_FRAME==3 -> FN_EN_ANIM_SOUND e,9,2;
// TakeoffStep frame C/17 same). PC family/cue reuse PS1 bank/sound numbers.
constexpr int kWingBank = 9;
constexpr int kWingSound = 2;
constexpr int kWingFrame = 3;

}  // namespace ps1::crow
