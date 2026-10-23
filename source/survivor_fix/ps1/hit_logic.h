// PS1 logic of a living zombie being hit: which reaction and how far it slides.
//
// Source: PS1 zombie code bank at 80100000 (ENEMY_SLOT40), read in Ghidra on 2026-10-21.
//   - 80105180: with bit E+320 & 0x10 (hit) it calls 801012CC; if the zombie stays alive
//     (80102224) the reaction is chosen by 80101474.
//   - 80101474: with E+320 & 0x1000 (state 6 after the bite: 0x20 and 08) it always returns
//     0x20. Otherwise, by the current clip E+85E:
//       0-5, 0x14, 0x1B, 0x1C, 0x1F, 0x26-0x29: roll r = rng & 0xFF, row 4 if r == 0,
//         row 0 up to 0x14, row 2 up to 0x19, row 3 up to 0x1E, then row 4;
//       6, 8, 0xC, 0x15: the current clip (the reaction goes on);
//       7, 0xA, 0xB and all the others: row 4.
//     Row 4 = no reaction (result | 0x8000: 801012CC restores state and substate).
//     Rows 0-3: table 801059E0 at index row*4 + (hit part of type 7-10 ? 2 : 0)
//     + (hit from behind, E+32B 2-5 ? 1 : 0); P+0C = 1000 if d <= 5000, 500 up to 10000,
//     200 beyond.
//   - 80102D74 (state 6) substate 0, per clip: 06/0A P+10 += P+0C/2; 07/0B P+10 += P+0C;
//     08/0C P+10 += P+0C*6/5; 0x20 P+10 = 500; 0x15 no push. The push is then
//     (P+10/100) * P+14 per frame, P+14 = 0x14 dropping by 2 (backfall_logic.h), away from the
//     shooter (front: backwards, behind: forwards). At the end of the clip: state 4.
#pragma once

namespace ps1 {

enum class HitKind {
  Knockback,  // 0x20 again (bit 0x1000)
  Table,      // roll on table 801059E0
  Keep,       // the running reaction goes on, with another push
  None,       // no reaction
};

HitKind ClassifyHit(int clip, bool knockbackBit);

constexpr int kHitNoReaction = 4;
// Table row for roll r (0-255): 0, 2, 3 or kHitNoReaction.
int HitTableRow(int rngByte);
// Clip from table 801059E0; -1 for row 4.
int HitTableClip(int row, bool limb, bool behind);

// P+0C by distance (8007F4F4).
int HitSlideBase(float distance);
// P+10 after substate 0 of 80102D74 for the reaction clip (current = P+10 before).
int HitSlide(int clip, int slideBase, int current);
// The clip pushes in substate 1 (0x15 and the clips without a case go straight to substate 2).
bool HitPushes(int clip);

}  // namespace ps1
