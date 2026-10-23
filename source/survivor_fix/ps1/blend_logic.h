// PS1 blend on a clip change, converted to the PC transition.
//
// Source: TEST.BIN, read in Ghidra on 2026-10-21.
//   - 80080334(E, clip, n) and 8008035C(E, state, clip, n) set E+8B0 = n; 800802C8(E, state,
//     clip) sets E+8B0 = 0x180 (1 if the clip has bit 0x8000).
//   - 8007F6AC, on the second frame of the change: for every track the command counter (+0x10,
//     +0x12) restarts from E+8B0 and the track blends from the current pose to the first command
//     of the new clip; 8007E38C removes +0x0D (0x10 at 1x) per frame. So n = 4 lasts 1 frame,
//     0x180 lasts 24 frames (DuckStation 2026-10-21: 0x1B/0x20/08/0F start at once, 0E stays
//     24 frames on its first command).
//   - PC: 00436B40 -> 00412B90(M, track, selector, transition, speed) with transition always 10
//     (2 for type 0x1E); the blend drops by |speed| * 0.5 per update, i.e. ~7 updates at 2.93
//     (= 1x PS1, 0x10 per frame).
#pragma once

namespace ps1 {

constexpr int kBlendStateChange = 0x180;  // 800802C8
constexpr int kPs1UnitsPerFrame = 0x10;   // 8007E38C at 1x

// PC transition equivalent to `ps1Units` of PS1 blend at the PC speed `rate`.
// At least 1: with 0 the PC does not blend and the pose would jump.
int PcTransition(int ps1Units, float rate);

// 80104108, LAB_801042A8: while track Q1 is on the sentinel key (+0x14 == +0x0E, 11686), i.e.
// while the new clip is blending, E+87C/E+884 = 0: the zombie does not move. The check sees the key
// from before the frame advances, so the still frames are those of the blend plus one.
// DuckStation 2026-10-22: blend 4 (0x20, 08) -> 2 still frames; 0x180 (02 -> 04, 08 -> 0E, 0F -> 02) -> 25.
int BlendStillFrames(int ps1Units);

}  // namespace ps1
