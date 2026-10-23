#include "blend_logic.h"

#include <cmath>

namespace ps1 {

int PcTransition(int ps1Units, float rate) {
  // The PC uses |rate| * 0.5 units per update, the PS1 0x10 per frame; one update per frame.
  const float pc = static_cast<float>(ps1Units) * std::fabs(rate) * 0.5f /
                   static_cast<float>(kPs1UnitsPerFrame);
  const int units = static_cast<int>(std::lround(pc));
  return units < 1 ? 1 : units;
}

int BlendStillFrames(int ps1Units) {
  if (ps1Units < 0) return 0;
  return (ps1Units + kPs1UnitsPerFrame - 1) / kPs1UnitsPerFrame + 1;
}

}  // namespace ps1
