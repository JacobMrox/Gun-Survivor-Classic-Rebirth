#include "wall_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ps1 {

namespace {

// MIPS mult/mflo: product truncated to 32 bits.
int32_t Mul(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

int32_t Abs(int32_t v) { return v < 0 ? -v : v; }

// (v << 12) >> 12 of 8007BFF0: sign extension from 20 bits.
int32_t Sext20(int32_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v) << 12) >> 12; }

// Safety limit on the loop of 8007C60C (the PS1 has none).
constexpr int kMaxNudges = 4096;

// 8007C60C (with 8007BFF0): brings p1 onto the wall's line and to the right side.
void Respond(const Wall& w, WallPoint& p1) {
  int32_t lx = p1.x, lz = p1.z;
  if (w.x2 != w.x1 || w.z2 != w.z1) {
    if (w.x2 == w.x1) lx = Sext20(w.x1);
    if (w.z2 == w.z1) lz = Sext20(w.z1);
  }
  uint32_t dir = (static_cast<uint32_t>(w.x2 - w.x1) >> 31) << 1;
  if (w.z2 - w.z1 < 0) dir |= 1;
  for (int k = 0; k < kMaxNudges; ++k) {
    if (Mul(w.z2 - w.z1, lx - w.x1) - Mul(w.x2 - w.x1, lz - w.z1) < 1) break;
    switch (dir) {
      case 0:
        lx -= 1;
        lz += 1;
        break;
      case 1:
        lx += 1;
        lz += 1;
        break;
      case 2:
        lx -= 1;
        lz -= 1;
        break;
      default:
        lx += 1;
        lz -= 1;
        break;
    }
  }
  p1.x = lx;
  p1.z = lz;
}

}  // namespace

int StepAgainstWalls(const std::vector<Wall>& walls, WallPoint p0, WallPoint& p1) {
  const int n = static_cast<int>(walls.size());
  int hits = 0;
  int last = -1;
  for (int i = 0; i < n; ++i) {
    const Wall& w = walls[i];
    if (Abs(p1.z - w.z1) >= 60001 || i == last) continue;
    const int32_t dxw = w.x2 - w.x1;
    const int32_t den = Mul(p1.z - p0.z, dxw) - Mul(p1.x - p0.x, w.z2 - w.z1);
    if (den == 0) {
      if (hits > 2) return hits;
      continue;
    }
    const int32_t a = w.x1 - p0.x;
    const int32_t t = Mul(w.z1 - p0.z, p1.x - p0.x) - Mul(a, p1.z - p0.z);
    const int32_t u = Mul(w.z1 - p0.z, dxw) - Mul(a, w.z2 - w.z1);
    if (Abs(t) >= Abs(den) || (t < 1 ? -den : den) < 0) continue;
    if (Abs(u) >= Abs(den) || (u < 1 ? -den : den) < 0) continue;
    Respond(w, p1);
    ++hits;
    last = i;
    i = i < 5 ? 0 : i - 4;  // then ++i: it restarts from wall 1, or 3 back
    if (hits > 2) return hits;
  }
  return hits;
}

int StepAgainstEnemyWalls(const std::vector<Wall>& walls, const std::vector<Wall>& enemyOnly,
                          WallPoint from, WallPoint& to) {
  int hits = StepAgainstWalls(walls, from, to);
  if (hits > 2) return hits;  // original early return before N4
  const auto sub = [](int32_t a, int32_t b) { return static_cast<int32_t>(uint32_t(a) - uint32_t(b)); };
  const auto crosses = [](int32_t a, int32_t b) { return a == 0 || b == 0 || (a < 0) != (b < 0); };
  for (const auto& w : enemyOnly) {
    if (std::abs(int64_t(to.z) - w.z1) >= 60001) continue;
    const int32_t dz = sub(w.z2, w.z1), dx = sub(w.x2, w.x1);
    const int32_t mx = sub(to.x, from.x), mz = sub(to.z, from.z);
    const int32_t a = sub(Mul(mz, sub(w.x1, from.x)), Mul(mx, sub(w.z1, from.z)));
    const int32_t b = sub(Mul(mz, sub(w.x2, from.x)), Mul(mx, sub(w.z2, from.z)));
    const int32_t end = sub(Mul(dz, sub(to.x, w.x1)), Mul(dx, sub(to.z, w.z1)));
    const int32_t begin = sub(Mul(dz, sub(from.x, w.x1)), Mul(dx, sub(from.z, w.z1)));
    const uint32_t sum = uint32_t(a) + uint32_t(b) + uint32_t(end) + uint32_t(begin);
    if (end >= 0 && crosses(a, b) && crosses(end, begin) && sum != 0) {
      Respond(w, to);
      ++hits;
    }
  }
  return hits;
}

bool InNegativeZDoorPassage(const std::vector<Wall>& walls, float minX, float maxX,
                            float nativeExitZ, float x, float pcZ) {
  if (!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(nativeExitZ) ||
      !std::isfinite(x) || !std::isfinite(pcZ) || minX >= maxX || x < minX || x > maxX) return false;
  bool found = false;
  float entranceZ = 0.0f;
  for (const auto& wall : walls) {
    if (wall.z1 != wall.z2 || wall.x1 == wall.x2 ||
        (std::min)(wall.x1, wall.x2) > minX || (std::max)(wall.x1, wall.x2) < maxX) continue;
    const float z = -static_cast<float>(wall.z1);
    if (z <= nativeExitZ) continue;
    if (!found || z < entranceZ) {
      entranceZ = z;
      found = true;
    }
  }
  return found && pcZ <= entranceZ;
}

bool InNegativeXDoorPassage(const std::vector<Wall>& walls, float minPcZ, float maxPcZ,
                            float nativeExitX, float x, float pcZ) {
  if (!std::isfinite(minPcZ) || !std::isfinite(maxPcZ) || !std::isfinite(nativeExitX) ||
      !std::isfinite(x) || !std::isfinite(pcZ) || minPcZ >= maxPcZ || pcZ < minPcZ || pcZ > maxPcZ) return false;
  bool found = false;
  float entranceX = 0.0f;
  for (const auto& wall : walls) {
    if (wall.x1 != wall.x2 || wall.z1 == wall.z2 ||
        -static_cast<float>((std::max)(wall.z1, wall.z2)) > minPcZ ||
        -static_cast<float>((std::min)(wall.z1, wall.z2)) < maxPcZ) continue;
    const float wallX = static_cast<float>(wall.x1);
    if (wallX <= nativeExitX) continue;
    if (!found || wallX < entranceX) {
      entranceX = wallX;
      found = true;
    }
  }
  return found && x <= entranceX;
}

std::vector<DoorEvent> ParseDoorEvents(const uint8_t* data, size_t size) {
  std::vector<DoorEvent> doors;
  for (size_t off = 0; data && off + kEventRecordSize <= size; off += kEventRecordSize) {
    int16_t type, radius;
    int32_t x, z;
    std::memcpy(&type, data + off, sizeof type);
    std::memcpy(&x, data + off + 4, sizeof x);
    std::memcpy(&z, data + off + 0xC, sizeof z);
    std::memcpy(&radius, data + off + 0x1E, sizeof radius);
    if (type == kDoorEventType && radius > 0) doors.push_back({x, z, radius});
  }
  return doors;
}

float DistanceToNearestWall(const std::vector<Wall>& walls, float x, float pcZ) {
  const double px = x, pz = -pcZ;
  double best = 1e30;
  for (const Wall& w : walls) {
    const double ax = w.x1, az = w.z1, bx = w.x2 - ax, bz = w.z2 - az;
    const double len = bx * bx + bz * bz;
    double t = len > 0.0 ? ((px - ax) * bx + (pz - az) * bz) / len : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const double dx = px - (ax + bx * t), dz = pz - (az + bz * t);
    const double d = std::sqrt(dx * dx + dz * dz);
    if (d < best) best = d;
  }
  return static_cast<float>(best);
}

bool MeshWallMatchesPs1(const std::vector<Wall>& walls, float x, float pcZ, float nx, float nz, float maxDist) {
  const double px = x, pz = -pcZ;
  const double hx = nx, hz = -nz;  // normal in PS1 coordinates (opposite z)
  const double hl = std::sqrt(hx * hx + hz * hz);
  if (!(hl > 1e-6)) return false;
  for (const Wall& w : walls) {
    const double ax = w.x1, az = w.z1, bx = w.x2 - ax, bz = w.z2 - az;
    const double len = std::sqrt(bx * bx + bz * bz);
    if (!(len > 0.0)) continue;
    double t = ((px - ax) * bx + (pz - az) * bz) / (len * len);
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const double dx = px - (ax + bx * t), dz = pz - (az + bz * t);
    if (std::sqrt(dx * dx + dz * dz) > maxDist) continue;
    // Face parallel to the segment: its normal is almost perpendicular to the wall's direction.
    if (std::fabs((hx * bx + hz * bz) / (hl * len)) < 0.5) return true;
  }
  return false;
}

bool InDoorEvent(const std::vector<DoorEvent>& doors, float x, float pcZ) {
  if (!std::isfinite(x) || !std::isfinite(pcZ)) return false;
  const int64_t px = static_cast<int64_t>(std::lround(x));
  const int64_t pz = -static_cast<int64_t>(std::lround(pcZ));
  for (const DoorEvent& d : doors) {
    const int64_t dx = px - d.x, dz = pz - d.z;
    if (dx > 1000 || dx < -1000 || dz > 1000 || dz < -1000) continue;  // 0x3E9, 1000
    const int64_t dist = static_cast<int64_t>(std::sqrt(static_cast<double>(dx * dx + dz * dz)));
    if (dist < d.radius) return true;
  }
  return false;
}

}  // namespace ps1
