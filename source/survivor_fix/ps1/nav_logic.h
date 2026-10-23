// PS1 logic of the zombie chase (state 4, 801027C0), direction and step.
//
// Source: PS1 zombie code bank at 80100000, read in Ghidra.
//   - line of sight:  80100AE8 (segment a->b against the ROOMENV walls, one active side)
//   - clear line:     8007F210 (walks and steers towards the player, 6..0x28 units per frame)
//   - going around:   80100E24 (probes ahead and at +/-90 degrees, turning on the spot by 0x16)
//   - movement:       8007F404 (the velocity is cleared every frame: no step, no movement)
// Everything is in the PS1 space of ROOMENV.DAT: the caller converts the PC coordinates
// (x_PS1 = x_PC, z_PS1 = -z_PC, measured in game in room 7/0).
// Vectors are on the XZ plane.
#pragma once

#include <cstdint>
#include <vector>

namespace ps1 {

struct Wall {
  int32_t x1, z1, x2, z2;
};

struct Vec2 {
  float x, z;
};

// 80100AE8: index of the first wall that segment a->b crosses starting from the wall's
// active side (cross product <= 0), or -1. With ignoreNear > 0 it skips the walls closer
// than ignoreNear to b.
int FirstBlockingWall(const std::vector<Wall>& walls, Vec2 a, Vec2 b, float ignoreNear = 0.0f);

// Constants of 80100E24 / 80100C90 / 80100924 / 8007F210. Angles in radians.
constexpr float kAngleUnit = 2.0f * 3.14159265f / 4096.0f;
constexpr float kTurnStep = 0x16 * kAngleUnit;                     // 1.93 degrees
constexpr float kSideProbeAngle = 0x400 * kAngleUnit;              // 90 degrees
constexpr float kForwardConeHalfAngle = 0x5A * 0x0B * kAngleUnit;  // 87 degrees
// 8008286C(rot, out, len) rotates (0,0,len<<8) and scales >>8: a vector of length len.
// The probe of 80100C90 is therefore 100 * 0x14.
constexpr float kProbeDistance = 100.0f * 0x14;
constexpr float kNearWallDistance = 500.0f;  // 80100E24: 80100DC8 < 500

// 8007F210: angle units per frame from cos(velocity, towards the player) * 10.
// Index 0 (and negative cosine): 0x28; 9: 6.
int SteerUnits(float cosine);

// 8002A3D8 (PsyQ SquareRoot0): integer root from the table at 8005C06C, 192 entries equal to
// floor(sqrt((i + 64) / 64) * 4096) (compared with the PS1 RAM on 2026-10-22). Rounds down.
int32_t Ps1Sqrt(int32_t a);

// 8007F210 in integers, one clear-line chase frame: signed rotation to add to E+1C6 (4096 per
// turn), 0 if the integer cosine exceeds 0x1000.
//   - E+87C = 8008286C(E+1C4, E+866): (0, 0, speed << 8) rotated by yaw, >> 8 rounded;
//   - cosine = (v . d) << 12 / (8002A3D8(|v|^2) * 8002A3D8(|d|^2)), d = player - E+198/E+1A0 in 16 bits;
//   - <= 0x1000: 0x28..6 like SteerUnits, direction from 8007C04C (<= 0: to the right).
// The truncated roots push it past 0x1000 when the zombie is almost aligned: there the PS1 does not
// turn (DuckStation 2026-10-22, 177 walking frames out of 177 identical). speed is E+866, negative on the PS1.
int Ps1SteerUnits(int32_t x, int32_t z, int yaw, int speed, int32_t px, int32_t pz);

enum class NavMode { Straight = 0, TurnLeft = 1, TurnRight = 2 };

struct NavState {
  NavMode mode = NavMode::Straight;  // P+0
};

struct NavInput {
  Vec2 pos;      // zombie
  Vec2 forward;  // current direction of the zombie (E+1C6)
  Vec2 player;   // target
};

struct NavOutput {
  bool lineOfSight;  // no wall between zombie and player
  Vec2 heading;      // new direction (normalized)
  bool walk;         // true: moves (E+87C = forward * E+866); false: turns on the spot
  bool turnLeft;     // with walk == false: turn direction (+90 degrees = left)
};

struct NavParams {
  // 8007F210 redone in integers (Ps1SteerUnits) with speed ps1Speed (E+866): no limit on the
  // missing angle, like the PS1. Without it: floating point rotation limited to the angle.
  bool ps1IntegerSteer = false;
  int ps1Speed = 0;
  float probeDistance = kProbeDistance;
  float turnStep = kTurnStep;
  // Multiplies all rotations per update: the PS1 works per frame, the PC per enemy logic update.
  float rateScale = 1.0f;
  // PC adaptation. On the PS1 the player also moves against these walls (8007CB9C, called
  // from 800785C0/8008369C), so their centre never enters the margin between the line and the
  // real wall. On the PC the player collides with the meshes and does enter it: the line of
  // sight ignores the walls closer than this to the player.
  float playerWallMargin = 1000.0f;
};

// One state 4 update (801027C0 -> 80100AE8 -> 8007F210 or 80100E24).
// "Left" is the +90 degree rotation in XZ; the rotation direction follows the choices of
// 8007F210/80100E24 (towards the player's side) regardless of the PS1 angle convention.
NavOutput NavStep(NavState& state, const std::vector<Wall>& walls, const NavInput& in,
                  const NavParams& params = NavParams());

Vec2 Rotate(Vec2 v, float radians);
float DistanceToWall(Vec2 p, const Wall& w);

}  // namespace ps1
