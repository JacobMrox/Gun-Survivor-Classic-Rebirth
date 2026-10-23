// PS1 blood when the zombie is hit (effect 0x1F, TEST.BIN read on 2026-10-22).
//   - 80070628: the point is the crosshair's at a depth of 5/6 of the zombie's distance
//     (8009B71C with ((E+868 * 5 * 0xAAAAAAAB) >> 32 << 14) >> 16).
//   - 80099068: 80098608 (sequence A, 800A7578) and/or 800987F8 (sequence B, 800A7580).
//   - 800936E0 / 8009414C: every frame has a cell (side in texels) and a duration; the side is scaled
//     by +34 = 0xAA / 256 (bit 8 of flags 0x41C).
#pragma once

namespace ps1 {

enum class BloodSequence { A, B };

struct BloodFrameInfo {
  int frame;  // index in the sequence, -1 once the sequence is over
  int cell;   // cell side in texels
};

// Total frames: A 29, B 17.
int BloodDuration(BloodSequence seq);
int BloodFrameCount(BloodSequence seq);
// Frame shown `age` frames after creation (0 = the first).
BloodFrameInfo BloodFrameAt(BloodSequence seq, int age);
// Cell of the PC pictures (blood1/blood2: `cells` cells from dense to sparse) for the PS1 frame at `age`:
// the first while the PS1 cell grows, then the others spread over the full-size frames (A: 6, like the
// PC cells; B: 4). -1 once the sequence is over.
int BloodPcCell(BloodSequence seq, int age, int cells);
// Side scaled like 800936E0: cell * 0xAA >> 8.
int BloodScaledSide(int cell);
// Depth of the point (80070628) relative to the zombie's distance.
int BloodDepth(int distance);

}  // namespace ps1
