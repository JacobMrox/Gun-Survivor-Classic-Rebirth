// PS1 logic of the zombie bite: when it starts, one at a time, grab from behind.
//
// Source: PS1 zombie code bank at 80100000 (ENEMY_SLOT40), read in Ghidra on 2026-10-15.
//   - 80102828 (state 4, band 4): with distance <= 1000 counter E+360 grows by 1 per
//     frame if the zombie is in the cone in front of the player (E+320 & 0x40), otherwise it goes
//     back to 0; at 16 the bite starts (if 80100628 allows it). The counter restarts from 0 when
//     the zombie enters state 4 again.
//   - 800824B0: cone between the rays at +/-0x226 units (4096 = one turn) from the player's
//     direction.
//   - 80100628: no other zombie is biting (bit 0x20000).
//   - 80101948: grab from behind with distance <= 1000, zombie outside the cone, global wait
//     80105FC0 at 0; then the wait goes back to 240 frames.
#pragma once

#include "nav_logic.h"

namespace ps1 {

constexpr float kBiteRange = 1000.0f;
constexpr int kBiteFrames = 16;
constexpr float kBiteConeHalfAngle = 0x226 * kAngleUnit;  // 48.3 degrees
constexpr int kBackGrabCooldown = 240;
// 80078A58 with mode 1 (called from 80103FA4 through 80078D38(E,0,dist,2,1)): the view
// turns towards the zombie by 0x61 units per frame (8.5 degrees), and a rest below 12 units
// is dropped. TODO: pitch towards the zombie's height (0x2B per frame).
constexpr float kBackGrabTurnStep = 0x61 * kAngleUnit;
constexpr float kBackGrabTurnDone = 12 * kAngleUnit;

// Frame by frame grab rotation, like 80078A58 (setup) and 80079064 (apply): `frames`
// steps of 0x61, then the tail: if the rest is >= 7 it adds rest-6, then 6, 5, 4, 3, 2, 1.
// A rest < 12 is dropped. Units are 4096 per turn.
struct GrabTurn {
  int frames = 0;  // 800B4E20
  int rest = 0;    // 800B4E24
  int sign = 1;    // 800A85B8: 1 = positive direction
  int step = 0x61;  // 800B4E18: 0x61 mode 1 (grab from behind), 0x7A mode 0 (bite)
  bool active = false;
};
GrabTurn StartGrabTurn(int angleUnits, int step = 0x61);
// Units (signed) to add in this frame; at the end of the rotation active becomes false.
int StepGrabTurn(GrabTurn& t);

// Full plan of 80078A58: horizontal rotation at `yawStep` per frame with the tail, pitch
// towards the target spread over the same frames (800B4E1C). If the pitch needs more frames
// (|pitch| / pitchStep), the horizontal rotation is spread over those frames without a tail.
// Mode 1 (grab from behind): 0x61 / 0x2B. Mode 0 (bite): 0x7A / 0x36. The pitch is applied
// only on the "full" frames (80079064, branch 800B4E20).
struct ViewTurn {
  GrabTurn yaw;
  int pitchStep = 0;  // units per frame, signed
};
ViewTurn StartViewTurn(int yawUnits, int pitchUnits, int yawStep, int pitchStepMax);
// Yaw and pitch units of this frame; done when yaw.active is false.
void StepViewTurn(ViewTurn& v, int& yawUnits, int& pitchUnits);

// 800824B0: the zombie is within kBiteConeHalfAngle of the player's direction.
bool InBiteCone(Vec2 player, Vec2 playerForward, Vec2 zombie);

struct BiteCounter {
  int frames = 0;  // E+360
};

// One frame/update of band 4 of 80102828. Returns true when the bite may start:
// counter >= 16 and distance <= 1000 (the PS1 checks the counter only in band 4).
// Out of range the counter stays as it is.
bool UpdateBiteCounter(BiteCounter& c, float distance, bool inCone);

// 80081C80 -> 80081FF0: while the player walks or runs (800785C0, bits 0/1 of 800B6080)
// their velocity gets, for every enemy, the push out of the circle of radius 600 (player,
// 800BB044) + the enemy's radius (E+8C0 = 800829D8(E,10); zombie 380, table 80105728), if the
// height difference is within +/-800. The push is
// (player - enemy) * (radius - distance) / (distance + 1), in integers.
constexpr int kPlayerPushRadius = 600;
constexpr int kZombieRadius = 380;
constexpr int kPushHeight = 800;
// Push for the player (dx, dz) away from the enemy; (0, 0) outside the circle.
Vec2 PlayerPush(Vec2 player, Vec2 enemy, float dy, int enemyRadius);

// 80101948, without the wait: distance and position outside the cone.
bool BackGrabInRange(float distance, bool inCone);

// Grab from behind on the PC: the player turns towards the zombie by at most maxStep
// radians per update (on the PS1 the view event 80078D38 does it). Returns the new
// normalized direction; done = true once it is within doneAngle.
struct TurnResult {
  Vec2 forward;
  bool done;
};
TurnResult TurnToward(Vec2 forward, Vec2 target, float maxStep, float doneAngle);

}  // namespace ps1
