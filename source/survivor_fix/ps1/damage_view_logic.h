// PS1 logic of the view jolt when the zombie bites (bitten player sequence).
//
// Source: TEST.BIN (8006C118), 800830F8 steps 0-2 and 5, read on 2026-10-17; measured in DuckStation
// the same day.
//   - 800A865E: direction offset, added to 800A8606 in the player's matrix (8009B71C, rotation
//     (800A865C, 800A8606 + 800A865E)); the view uses the player as reference.
//     Step 0: 800BB078 = -50; then every frame + or - 50 between 100 and 300 (800BB074 even/odd), until
//     800BB074 = 3: 100, 150, 200, 250, 300, 300, 250, 200, 150, 100, 100, 100.
//   - 800B3EF8: y of the view's look-at point (800B3EE8, GsRVIEW2 of 80033FD8: look-at point
//     (0, 500, -3000) from the player, 8006DD5C). Step 1: -250 per frame down to -1000 (head back).
//   - step 5 (frame 12): offset 0, y 500, then the view turns towards the zombie (80078D38).
#pragma once

namespace ps1 {

constexpr int kViewLookY = 500;       // 8006DD5C: 800B3EF8 at rest
constexpr int kViewLookZ = -3000;     // 8006DD5C: 800B3EFC
constexpr int kBiteJoltFrames = 12;   // frames 0..11, step 5 at 12

struct BiteJolt {
  int frame = 0;
  int phase = 0;     // 800BB074
  int yawOff = 0;    // 800A865E (4096 per turn)
  int lookY = kViewLookY;  // 800B3EF8
  bool done = false;
};

// One frame of 800830F8 (steps 0-2, then 5). After step 5 yawOff stays 0 and lookY 500.
void StepBiteJolt(BiteJolt& j);

}  // namespace ps1
