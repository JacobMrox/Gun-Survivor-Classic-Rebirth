// PS1 size of the zombie shadow.
#pragma once

#include <cmath>
#include <cstring>

namespace ps1 {

// TEST.BIN 80073C5C, table 800A0EC4: zombie half extents 280, 280; the PC (4361B0) uses 500.
// Row-vector local scaling only: centre, Y and translation stay, opacity does not change.
inline bool ZombieShadowMatrix(const float* original, float* output) {
  if (!original || !output || original == output) return false;
  for (int i = 0; i < 16; ++i)
    if (!std::isfinite(original[i])) return false;
  std::memcpy(output, original, 16 * sizeof(float));
  constexpr float scale = 280.0f / 500.0f;
  for (int k = 0; k < 4; ++k) {
    output[k] *= scale;
    output[8 + k] *= scale;
  }
  return true;
}

}  // namespace ps1
