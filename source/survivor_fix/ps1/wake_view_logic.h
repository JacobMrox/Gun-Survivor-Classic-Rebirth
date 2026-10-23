// PS1 logic of the view during the opening wake-up (07/04, first scene of a new game).
//
// Source: TEST.BIN (8006C118), read on 2026-10-17. During events 8008CA90 copies the angles of the
// event camera into the player camera's (800A865C = 800BB564, 800A8606 = 800BB516), and 8009B71C
// rebuilds the camera from the absolute angles (RotMatrix of (800A865C, 800A8606, 800A8660)).
// The event script moves the angles with the commands of table 800A2C00 (0x20 = 8008A4E4, 0x21 =
// 8008A6A4): every frame it adds the difference from the look-at point divided by the frames left.
// The eye does not move (800BB4E8 = (5800, 0, 1000) for the whole event).
// Values recorded frame by frame in DuckStation on 2026-10-17, new NORMAL game: the event runs from
// frame 3 to 588, the camera turns from 188 to 325 and goes back to rx = 0 (the camera ends level),
// ry from 1024 to 935.
// The PC (00444560, calls to 0045E110 from 00456D17-00456E25) approximates it with 7 constant-step
// phases that leave +2.5 degrees of pitch: here is the PS1 curve.
#pragma once

namespace ps1 {

constexpr int kWakeViewFrames = 138;  // event frames 188-325

struct WakeAngles {
  float pitchDeg;  // PS1 rx in degrees: positive like rx of 0045E110 on the PC
  float yawDeg;    // from the starting direction, in the direction of ry of 0045E110 (the PS1 ry is inverted on the PC)
};

// Angles of frame k (0 .. kWakeViewFrames - 1); past the last one the last one stays.
WakeAngles WakeView(int frame);

}  // namespace ps1
