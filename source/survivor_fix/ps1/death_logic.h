// PS1 zombie death (TEST.BIN read on 2026-10-22, deaths recorded in DuckStation the same day).
//   - 801012CC: with health at 0 (80102224) the clip is chosen by 80101700, from the running clip E+85E or,
//     standing, from the type of the hit part (+0x39A of record E+392) and the shooter's sector (E+32B).
//   - 801012CC: the returns with 0x8000 go through 8008035C(E, 6, clip, 4) (blend 4), the others through
//     800802C8 (blend 0x180: the first key of 08 to the head lasts 384, 24 frames).
//   - 801032CC (state 7): at the end of the clip, 0x0C and 0x13 go on to 0x13 and 08 and 0x12 to 0x12 with half
//     probability (rand <= 0x7E / < 0x7F, 80080334(E, clip, 2)), which loops on the ground; without the follow-up
//     the track speed goes to 0 (substate 2) and the last pose stays.
//   Recorded (keys of track 1): torso 0x0B 17 keys, 33 frames -> 0x0C 7 keys -> 0x13 looping (4 keys) or still
//   on 0x0C; head 08 after 24 frames of blend 9 keys -> 0x12 looping or still.
#pragma once

namespace ps1 {

// PS1 parts (records from E+392): 0 head (small), 1 head / neck, 2 torso, 3-4 arms, 7-10 legs.
enum class DeathPart { Head, Body, Arm };

struct DeathPlan {
  int clips[2];  // in order; the second follows the first in the clip chain (0x0B -> 0x0C)
  int count;     // 0 = no PS1 clip (the running clip goes on)
  int blend;     // E+8B0 units of the first: 4 (8008035C) or 0x180 (800802C8)
};
constexpr int kDeathBlendDirect = 4;     // 8008035C(E, 6, clip, 4), returns with 0x8000
constexpr int kDeathBlendState = 0x180;  // 800802C8, the others

// 80101700. `armRoll` and `sideRoll` are the two rands for the arms (0x1F, 0xCC).
DeathPlan PickDeath(int currentClip, DeathPart part, bool behind, int armRoll = 0xFF, int sideRoll = 0);
// 801032CC substate 2: the next clip for the pose on the ground, or -1 if it stays.
int DeathSecond(int clip, int roll);
// The follow-up clip on the ground loops (0x12, 0x13).
bool DeathLoops(int clip);
constexpr int kDeathSecondBlend = 2;

}  // namespace ps1
