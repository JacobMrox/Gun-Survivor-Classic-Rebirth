#include "nav_logic.h"

#include <array>
#include <cmath>

namespace ps1 {

namespace {

Vec2 Normalize(Vec2 v) {
  const float len = std::sqrt(v.x * v.x + v.z * v.z);
  return len > 0.0f ? Vec2{v.x / len, v.z / len} : Vec2{0.0f, 0.0f};
}

int Probe(const std::vector<Wall>& walls, Vec2 pos, Vec2 dir, float distance) {
  return FirstBlockingWall(walls, pos, {pos.x + dir.x * distance, pos.z + dir.z * distance});
}

// 80100924(E, 0x5A) -> 8010078C: player in the cone of +/-87 degrees ahead.
bool PlayerInCone(Vec2 heading, Vec2 toPlayer) {
  const Vec2 t = Normalize(toPlayer);
  return heading.x * t.x + heading.z * t.z > std::cos(kForwardConeHalfAngle);
}

}  // namespace

int FirstBlockingWall(const std::vector<Wall>& walls, Vec2 a, Vec2 b, float ignoreNear) {
  // Same operations as 80100AE8, in double precision (32-bit int on the PS1).
  const double ax = a.x, az = a.z, bx = b.x, bz = b.z;
  for (size_t i = 0; i < walls.size(); ++i) {
    const double x1 = walls[i].x1, z1 = walls[i].z1, x2 = walls[i].x2, z2 = walls[i].z2;
    const double sideA = (z2 - z1) * (ax - x1) - (x2 - x1) * (az - z1);
    const int sa = sideA > 0 ? 1 : -1;
    const int sb = (z2 - z1) * (bx - x1) - (x2 - x1) * (bz - z1) < 1 ? -1 : 1;
    const int s1 = (bz - az) * (x1 - ax) - (bx - ax) * (z1 - az) < 1 ? -1 : 1;
    const int s2 = (bz - az) * (x2 - ax) - (bx - ax) * (z2 - az) < 1 ? -1 : 1;
    if (sa * sb < 0 && s1 * s2 < 0 && sideA < 1) {
      if (ignoreNear > 0.0f && DistanceToWall(b, walls[i]) < ignoreNear) continue;
      return static_cast<int>(i);
    }
  }
  return -1;
}

Vec2 Rotate(Vec2 v, float radians) {
  const float c = std::cos(radians), s = std::sin(radians);
  return {v.x * c - v.z * s, v.x * s + v.z * c};
}

float DistanceToWall(Vec2 p, const Wall& w) {
  const float dx = static_cast<float>(w.x2 - w.x1), dz = static_cast<float>(w.z2 - w.z1);
  const float len2 = dx * dx + dz * dz;
  float t = len2 > 0.0f ? ((p.x - w.x1) * dx + (p.z - w.z1) * dz) / len2 : 0.0f;
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  const float cx = w.x1 + t * dx - p.x, cz = w.z1 + t * dz - p.z;
  return std::sqrt(cx * cx + cz * cz);
}

int SteerUnits(float cosine) {
  // 8007F210: switch (cos * 4096 * 10 >> 12); negative cosine or < 0.1 -> 0x28.
  static const int kUnits[10] = {0x28, 0x24, 0x20, 0x1C, 0x18, 0x14, 0x10, 0x0C, 8, 6};
  if (cosine < 0.1f) return kUnits[0];
  const int index = static_cast<int>(cosine * 10.0f);
  return kUnits[index > 9 ? 9 : index];
}

int32_t Ps1Sqrt(int32_t a) {
  static const auto table = [] {
    std::array<int32_t, 192> t{};
    for (int i = 0; i < 192; ++i) {
      int64_t r = static_cast<int64_t>(std::sqrt(double(i + 64) * 262144.0));
      while (r * r > int64_t(i + 64) * 262144) --r;
      while ((r + 1) * (r + 1) <= int64_t(i + 64) * 262144) ++r;
      t[i] = static_cast<int32_t>(r);
    }
    return t;
  }();
  // GTE LZCS/LZCR: leading zeros (ones for a negative value, which does not occur here).
  int n = 0;
  for (uint32_t u = static_cast<uint32_t>(a); n < 32 && !(u & 0x80000000u); u <<= 1) ++n;
  if (a < 0) n = 0;
  if (n == 32) return 0;
  n &= ~1;
  const uint32_t x = n < 24 ? static_cast<uint32_t>(a) >> (24 - n) : static_cast<uint32_t>(a) << (n - 24);
  return static_cast<int32_t>(static_cast<uint32_t>(table[x - 0x40] << ((31 - n) >> 1)) >> 12);
}

int Ps1SteerUnits(int32_t x, int32_t z, int yaw, int speed, int32_t px, int32_t pz) {
  // rsin/rcos at 4096. ASSUMPTION: the PsyQ table (rcossin_tbl) matches the rounding.
  const double a = (yaw & 0xFFF) * (2.0 * 3.14159265358979 / 4096.0);
  const int32_t s = static_cast<int32_t>(std::lround(std::sin(a) * 4096.0));
  const int32_t c = static_cast<int32_t>(std::lround(std::cos(a) * 4096.0));
  const int32_t len = speed * 256;
  auto round8 = [](int32_t v) { return (v >> 8) + (((v & 0xFF) >> 4) > 7 ? 1 : 0); };  // 8008286C
  const int32_t vx = round8((s * len) >> 12), vz = round8((c * len) >> 12);
  auto s16 = [](int32_t v) { return static_cast<int32_t>(static_cast<int16_t>(v)); };  // lhu and sll/sra 16
  const int32_t dx = s16(px - x), dz = s16(pz - z);
  auto wrap = [](int64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); };  // mult/mflo
  int32_t m = wrap(int64_t(Ps1Sqrt(wrap(int64_t(vx) * vx + int64_t(vz) * vz))) *
                   Ps1Sqrt(wrap(int64_t(dx) * dx + int64_t(dz) * dz)));
  if (m == 0) m = 1;
  const int32_t num = wrap(int64_t(wrap(int64_t(vx) * dx + int64_t(vz) * dz)) << 12);
  const int32_t cosine = num / m;  // div: towards zero
  if (cosine >= 0x1001) return 0;
  static const int kUnits[10] = {0x28, 0x24, 0x20, 0x1C, 0x18, 0x14, 0x10, 0x0C, 8, 6};
  const int index = (cosine * 10) >> 12;
  const int units = index >= 1 && index <= 9 ? kUnits[index] : 0x28;  // default: 0x28 (also 0x1000)
  // 8007C04C(E+198, E+198 + v, player): <= 0 -> negative direction.
  const int64_t cross = int64_t(vz) * (int64_t(px) - x) - int64_t(vx) * (int64_t(pz) - z);
  return wrap(cross) >= 1 ? units : -units;
}

NavOutput NavStep(NavState& state, const std::vector<Wall>& walls, const NavInput& in,
                  const NavParams& params) {
  const Vec2 toPlayer = {in.player.x - in.pos.x, in.player.z - in.pos.z};
  const Vec2 h = Normalize(in.forward);
  const Vec2 left = Rotate(h, kSideProbeAngle);
  // 8007C04C / 80100D40: side of the player relative to the direction (>= 0: on the left).
  const bool playerLeft = toPlayer.x * left.x + toPlayer.z * left.z >= 0.0f;
  NavOutput out;
  out.heading = h;
  out.walk = true;
  out.turnLeft = false;
  out.lineOfSight = FirstBlockingWall(walls, in.pos, in.player, params.playerWallMargin) < 0;

  if (out.lineOfSight) {
    // 801027C0 -> 8007F210: moves and steers towards the player.
    state.mode = NavMode::Straight;
    if (params.ps1IntegerSteer) {
      // PS1 direction: the motion is (sin, cos)(E+1C6) * E+866 with E+866 negative, so
      // (sin, cos)(yaw) = -h. PS1 positions are integers (truncated like zombie_world_pc.cpp).
      const float kUnitsPerRadian = 4096.0f / (2.0f * 3.14159265f);
      const int yaw = static_cast<int>(std::lround(std::atan2(-h.x, -h.z) * kUnitsPerRadian)) & 0xFFF;
      const int units = Ps1SteerUnits(static_cast<int32_t>(in.pos.x), static_cast<int32_t>(in.pos.z), yaw,
                                      params.ps1Speed, static_cast<int32_t>(in.player.x),
                                      static_cast<int32_t>(in.player.z));
      const float a = ((yaw + units) & 0xFFF) * kAngleUnit;
      out.heading = {-std::sin(a), -std::cos(a)};
      return out;
    }
    // Outside the PS1 world the rotation is limited to the missing angle.
    const Vec2 t = Normalize(toPlayer);
    const float cosine = h.x * t.x + h.z * t.z;
    float step = SteerUnits(cosine) * kAngleUnit * params.rateScale;
    const float missing = std::acos(cosine > 1.0f ? 1.0f : (cosine < -1.0f ? -1.0f : cosine));
    if (step > missing) step = missing;
    out.heading = Normalize(Rotate(h, playerLeft ? step : -step));
    return out;
  }

  if (state.mode == NavMode::Straight) {
    // 80100E24, P+0 == 0: goes on straight or chooses which way to turn.
    const int ahead = Probe(walls, in.pos, h, params.probeDistance);  // iVar5
    if (!(ahead == -1 && PlayerInCone(h, toPlayer))) {
      const int leftHit = Probe(walls, in.pos, left, params.probeDistance);  // iVar6
      const int rightHit = Probe(walls, in.pos, Rotate(h, -kSideProbeAngle),
                                 params.probeDistance);                      // iVar7
      NavMode next = NavMode::Straight;
      bool decided = false;
      if (!playerLeft) {
        if (rightHit == -1) next = NavMode::TurnRight, decided = true;
      } else if (leftHit == -1) {
        next = NavMode::TurnLeft, decided = true;
      }
      if (!decided) {  // LAB_80100EFC
        if (ahead == leftHit && ahead != -1) {
          next = NavMode::TurnRight, decided = true;
        } else if (ahead == rightHit && ahead != -1) {
          next = NavMode::TurnLeft, decided = true;
        } else if (leftHit == -1) {  // LAB_80100F3C
          if (rightHit != -1 && playerLeft) {
            next = NavMode::TurnLeft, decided = true;
          } else if (rightHit == -1) {
            next = playerLeft ? NavMode::TurnLeft : NavMode::TurnRight, decided = true;
          }
        } else if (rightHit == -1 && !playerLeft) {
          next = NavMode::TurnRight, decided = true;
        }
      }
      if (!decided && ahead != -1 && DistanceToWall(in.pos, walls[ahead]) < kNearWallDistance) {
        next = playerLeft ? NavMode::TurnLeft : NavMode::TurnRight;
      }
      state.mode = next;
    }
    if (state.mode == NavMode::Straight) return out;  // LAB_80101014 mode 0: moves straight
  }

  // LAB_80101014, modes 1/2: E+1C6 += / -= 0x16 without velocity (8007F404 clears it: it
  // turns on the spot), then probes ahead; clear -> mode 0 from the next update.
  const float step = params.turnStep * params.rateScale;
  out.turnLeft = state.mode == NavMode::TurnLeft;
  out.heading = Normalize(Rotate(h, out.turnLeft ? step : -step));
  out.walk = false;
  if (Probe(walls, in.pos, out.heading, params.probeDistance) == -1) {
    state.mode = NavMode::Straight;
  }
  return out;
}

}  // namespace ps1
