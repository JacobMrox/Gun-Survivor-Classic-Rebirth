// PS1 logic of the player's movement (digital pad).
//
// Source: TEST.BIN (8006C118), 800771AC and 800785C0, read on 2026-10-16; checked frame by frame in
// DuckStation.
//   - 800771AC: speed 800B3D48 = -0x50 (forward), -0x96 (forward while running), +0x3C (backwards, also
//     while running), fixed while the button is held: no acceleration.
//   - rotation 800B4DFA: with a turn button, 44 * 800A12EC / 10 (44 = (0x80 >> 1) * 7 / 10, the counter
//     grows up to 10 while used); without one, from beyond +/-6 it drops to +/-6, then by 1 per frame
//     to 0 (and the counter goes back to 0); if the direction flips, 0 and counter 0.
//   - 800785C0: direction 800A8606 += 800B4DFA (4096 units per turn), step along the direction.
#pragma once

namespace ps1 {

constexpr int kWalkForward = 0x50;   // units per frame
constexpr int kRunForward = 0x96;
constexpr int kWalkBack = 0x3C;
constexpr int kTurnMax = 44;
constexpr int kTurnCounterMax = 10;
constexpr int kTurnRelease = 6;

struct PlayerTurn {
  int rate = 0;     // 800B4DFA: units per frame, sign = direction
  int counter = 0;  // 800A12EC
  int previous = 0; // 800B4DFE
};

// One frame of 800771AC: input = -1, 0 or +1 (requested turn direction). Returns the
// rotation of the frame (4096 units per turn).
int StepTurn(PlayerTurn& t, int input);

}  // namespace ps1
