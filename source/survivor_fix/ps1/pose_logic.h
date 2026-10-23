// PS1 logic of the zombie's added pose: head towards the player and torso bend when hit.
//
// Source: PS1 zombie code bank at 80100000 (ENEMY_SLOT40), read on 2026-10-22.
//   - Per-node callback 801046F4 (index = (number - 1) / 20): node 2 (head) rotation +0x46 += E+364;
//     node 1 (torso) +0x48 += E+368; node 5 +0x48 -= E+368. On the PC: bones 3, 2 and 6 (bone tree:
//     3 = head, child of the torso, 6 = pelvis at the torso's point, parent of the legs).
//   - E+364, 80101130 (setup of states 4 and 6): point at 0x14 along heading + E+364, side of the player
//     relative to that line (8007C04C); E+364 += -0x14 if the result is > 0, otherwise +0x14, only
//     within +/-0x400. 801010EC (states 1-3 and 7): E+364 goes back to 0 by 0x14 per frame.
//   - E+368, 80101474 row 4 (hit without reaction): entries 16-19 of table 801059E0 (0, 1, 1, 0) -> -0x180
//     from the front, +0x180 from behind (hit part not of type 7-10); 80105180 brings it to 0 by 0x20 per frame.
// PS1 units: 4096 per turn.
#pragma once

namespace ps1 {

constexpr int kHeadTurnStep = 0x14;
constexpr int kHeadTurnLimit = 0x400;
constexpr int kTwistHit = 0x180;
constexpr int kTwistDecay = 0x20;

// One frame of 80101130. playerOnPositiveSide = 8007C04C(...) > 0.
int TrackHead(int headYaw, bool playerOnPositiveSide);
// One frame of 801010EC.
int RelaxHead(int headYaw);
// 80101474 row 4.
int TwistForHit(bool behind);
// One frame of 80105180.
int DecayTwist(int twist);

}  // namespace ps1
