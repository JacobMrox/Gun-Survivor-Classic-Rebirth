// PS1 logic of the view bob (eye height).
//
// Source: TEST.BIN (8006C118), 800785C0, read on 2026-10-16; tables in TEST.BIN, checked in
// DuckStation (800A8634 while walking: -1600 ... -1668, standing still it keeps the last value).
//   - walking (800B6080 bit 6 and bits 0/1): without a table one is chosen from the speed
//     800B3D48: backwards (> 0) 800A12CC, forward 800A128C, running (< -0x8B) 800A12B4; index 0.
//   - every frame 800A8634 = table[index] - 0x640, index + 1; at the final -1 the index goes back to 0
//     (step: 80084B04).
//   - without movement the table is cleared (LAB_80078910) and 800A8634 stays as it was.
//   - switching from running to walking (or back) clears the table (800771AC, 800B4DFC).
#pragma once

namespace ps1 {

enum class Gait { None, Walk, Run, Back };

struct HeadBob {
  const short* table = nullptr;  // 800B4DE4
  int index = 0;                 // 800B4DE8
  int offset = 0;                // 800A8634 + 0x640: 0 at rest, negative = eyes higher
  Gait gait = Gait::None;        // for the run/walk switch
};

// One frame of 800785C0. Returns the eye height offset (PS1 units, y pointing down).
int StepHeadBob(HeadBob& b, Gait gait);

}  // namespace ps1
