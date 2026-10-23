#pragma once

#include <cstdint>

namespace sf {

// PC asset compatibility, not PS1 gameplay. Called after the native animation update.
// The native FINE/CAUT animations share an index but contain 16/14 frames.
constexpr int32_t ConditionFrame(int32_t index, int16_t count) {
  return count <= 0 ? -1 : (index < 0 || index >= count ? 0 : index);
}

}  // namespace sf
