// PS1 logic of the walls for the player's step.
//
// Source: TEST.BIN (8006C118), read on 2026-10-17.
//   - 800785C0: velocity = step + enemy push (80081C80); n = 8007CB9C(pos, vel, 0);
//     if n == 0 pos += vel, otherwise pos = vel (8007C60C writes the corrected point there).
//   - 8007CB9C -> 8007C98C: the player is a point; segment pos -> pos + vel is intersected
//     in integers with the room's walls ([800B60CC], [800B60C8], x1 z1 x2 z2), skipping those with
//     |arrival z - z1| > 60000 and the last one hit; after a hit the search restarts from wall 1
//     (or 3 back) and ends at the third hit. With the third argument 0 (player) 8007C7A0 does not run.
//   - 8007C60C: the arrival point goes onto the line only for axis-aligned walls (8007BFF0),
//     then moves by 1 diagonally until it is on the side (z2 - z1)(x - x1) - (x2 - x1)(z - z1) <= 0.
// Checked in DuckStation: it stops exactly on lines x = 9504 and z = -9920 of 07/00.
#pragma once

#include <cstdint>
#include <vector>

#include "nav_logic.h"

namespace ps1 {

struct WallPoint {
  int32_t x, z;
};

// One step: p0 = position, p1 = position + velocity (in/out). Returns the number of hits
// (800B60BC); with hits p1 is the new position, without hits it stays pos + vel.
int StepAgainstWalls(const std::vector<Wall>& walls, WallPoint p0, WallPoint& p1);

// 8007CB9C(...,1): normal wall pass, then the one-sided inclusive N4 pass 8007C7A0.
int StepAgainstEnemyWalls(const std::vector<Wall>& walls, const std::vector<Wall>& enemyOnly,
                          WallPoint p0, WallPoint& p1);

// PC compatibility, not a replacement for 8007CB9C/8007C60C: a native door may be
// beyond the imported wall. Use the closest horizontal wall covering the entire
// native door span as the entrance; the PC keeps its own 3D collision/lock checks.
// Coordinates x/pcZ/nativeExitZ are PC world coordinates (z opposite ROOMENV).
bool InNegativeZDoorPassage(const std::vector<Wall>& walls, float minX, float maxX,
                            float nativeExitZ, float x, float pcZ);
// Same compatibility for a native exit towards -X; the lateral span is native PC Z.
// The entrance is the nearest vertical ROOMENV wall covering the complete span.
bool InNegativeXDoorPassage(const std::vector<Wall>& walls, float minPcZ, float maxPcZ,
                            float nativeExitX, float x, float pcZ);

// PS1 doors of the room: type 4 events (handler 8008DA74 -> 8008D51C in table 800A31B0).
// 0x24-byte records as 80079A28 reads them from [800AB52C]: type short +0, x int +4, z int +0xC,
// radius short +0x1E; on the PC they are section 4 of the room record in ROOMENV.DAT (07/00: the
// door to 08/01 at x 7800, z 5500, radius 600, in front of wall z 5056).
struct DoorEvent {
  int32_t x, z;
  int32_t radius;
};
constexpr int kDoorEventType = 4;
constexpr size_t kEventRecordSize = 0x24;
std::vector<DoorEvent> ParseDoorEvents(const uint8_t* data, size_t size);
// 80079A28: |dx| <= 1000, |dz| <= 1000 and integer sqrt(dx^2 + dz^2) < radius. pcZ is the PC z
// (opposite to the PS1 one).
bool InDoorEvent(const std::vector<DoorEvent>& doors, float x, float pcZ);

// PC adaptation, not a PS1 behaviour: distance of the point (x, PC z) from the nearest PS1 wall. The player's
// step against a mesh wall stays with the PS1 walls only where one is near: elsewhere (the side of the 08/02
// ramp, no segment in ROOMENV) the mesh stays solid.
float DistanceToNearestWall(const std::vector<Wall>& walls, float x, float pcZ);
// True if within maxDist there is a PS1 wall parallel to the mesh face with horizontal normal (nx, nz PC):
// the same wall. The base of the 08/02 ramp is perpendicular to the side walls and stays with the mesh.
bool MeshWallMatchesPs1(const std::vector<Wall>& walls, float x, float pcZ, float nx, float nz, float maxDist);

}  // namespace ps1
