#include "headbob_logic.h"

namespace ps1 {

namespace {

// TEST.BIN: view bob tables, terminated by -1.
constexpr short kWalk[] = {0,   -14, -26, -38, -42, -46, -50, -54, -60, -68,
                           -60, -54, -50, -46, -42, -38, -26, -14, 0,   -1};  // 800A128C
constexpr short kRun[] = {0, -44, -76, -92, -112, -112, -92, -76, -44, 0, -1};  // 800A12B4
constexpr short kBack[] = {0,   -20, -38, -46, -54, -60, -68,
                           -60, -54, -46, -38, -20, 0,   -1};  // 800A12CC

const short* TableFor(Gait g) {
  switch (g) {
    case Gait::Walk:
      return kWalk;
    case Gait::Run:
      return kRun;
    case Gait::Back:
      return kBack;
    default:
      return nullptr;
  }
}

}  // namespace

int StepHeadBob(HeadBob& b, Gait gait) {
  if (gait == Gait::None) {  // LAB_80078910: no table, the height stays
    b.table = nullptr;
    b.gait = gait;
    return b.offset;
  }
  // 800771AC: crossing the run threshold clears the table.
  if ((gait == Gait::Run) != (b.gait == Gait::Run)) b.table = nullptr;
  b.gait = gait;
  if (!b.table) {
    b.table = TableFor(gait);
    b.index = 0;
  }
  b.offset = b.table[b.index];
  ++b.index;
  if (b.table[b.index] == -1) b.index = 0;
  return b.offset;
}

}  // namespace ps1
