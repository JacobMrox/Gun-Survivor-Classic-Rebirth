// PS1 logic of the zombie backward fall: 0x20 -> 08 -> 0E -> 0F.
//
// Source: PS1 zombie code bank at 80100000 (ENEMY_SLOT40), read in Ghidra on 2026-10-15.
//   - start, 80102D74 (state 6), branch LAB_801030C0: with the current animation
//     0x20 and the Q1 cursor exactly at start(0x20)+10, if rng < 0xCC it runs
//     80080334(E,08,4) and P+14 = 0x14 (push).
//   - push, substate 3: every update E+87C += E+378 * P+14, then P+14 -= 2,
//     until P+14 <= 0. For 0x20, case 0x1A sets P+10 = 500 and
//     E+378 = forward(E+1C4) * (P+10/100) with the sign from +0x32B: magnitude 5.
//   - end of 08 (80100728): 800802C8(E,4,0E); end of 0E (80104108,
//     E+320 bit 8): 80080334(E,0F,2); end of 0F: locomotion.
#pragma once

namespace ps1 {

constexpr int kSelBackFallTrigger = 0x20;  // gesture during which the fall is decided
constexpr int kSelBackFall = 0x08;         // backward fall
constexpr int kSelSupine = 0x0E;           // lifts the torso from lying on the back
constexpr int kSelSitUp = 0x0F;            // gets up from sitting

// LAB_801030C0: ...+0x80 (entry 0x20) + 10 == Q1 cursor.
constexpr int kBackFallCheckFrame = 10;
// LAB_801030C0: (rng & 0xFF) < 0xCC, about 80%.
constexpr int kBackFallRngThreshold = 0xCC;
// Initial P+14 and decrement per update.
constexpr int kBackPushStart = 0x14;
constexpr int kBackPushStep = 2;
// |E+378| for 0x20: P+10 / 100 = 500 / 100.
constexpr float kBackPushMagnitude = 5.0f;

bool ShouldStartBackFall(int rngByte);

// LAB_801030C0 rolls on every state 6 frame in which the Q1 cursor of 0x20 is exactly
// start+10, not only once. On the real PS1 the 0x20 cursor advances one command every 3
// frames and stays on +10 for 2-3 frames: 2-3 rolls, ~99% fall (measured in DuckStation:
// 3 bites out of 3, one on the second roll).
bool IsBackFallCheckFrame(int cursor, int start, int frame = kBackFallCheckFrame);

struct BackPush {
  int strength = 0;                      // P+14
  float magnitude = kBackPushMagnitude;  // |E+378| = P+10 / 100 (integer)
  int delay = 0;                         // updates without push before the first one (substate 0)
  bool zeroLast = false;                 // substate 1: at P+14 < 1 it clears E+87C, the last one is lost
};

// Starts the push (80080334(E,08,4) sets P+14 = 0x14). For hit reactions the
// magnitude is P+10 / 100 (hit_logic.h).
void StartBackPush(BackPush& p, float magnitude = kBackPushMagnitude);

// State 6 push (0x20 and hit reactions), 80102D74: one substate 0 frame (P+10, E+378,
// P+14 = 0x14 if it was 0) without push, then substate 1, which clears E+87C/E+884 when P+14
// drops below 1: the 5 x 2 push never happens. With the clip's blend 4 (LAB_801042A8) the PS1
// moves 90, 80, ..., 20 = 440 (DuckStation 2026-10-22, 4 bites out of 4).
void StartKnockbackPush(BackPush& p, float magnitude = kBackPushMagnitude);

// Movement of this update in PS1 units (|E+378| * P+14), then P+14 -= 2.
// 0 once the push is over.
float NextBackPush(BackPush& p);

}  // namespace ps1
