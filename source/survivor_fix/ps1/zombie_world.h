// PS1 contacts between actors and enemy walls (shared PS1 zombie world).
#pragma once

#include <cstdint>
#include <vector>

#include "wall_logic.h"

namespace ps1 {

// TEST.BIN 80081618, 80081FF0, 8007F404. Coordinates are PS1 integers.
struct WorldVector {
  int32_t x = 0, y = 0, z = 0;
};

struct WorldActor {
  std::uintptr_t life = 0;
  WorldVector position, velocity;
  int radius = 380;
  uint32_t flags = 0;
  bool active = true;
};

WorldVector PlayerContactPush(const WorldActor& player, const std::vector<WorldActor>& ordered);
WorldVector ContactPush(const WorldActor& fixed, const WorldActor& moving);
void ResolveActorContacts(WorldActor& actor, const std::vector<WorldActor>& ordered,
                          const WorldActor* player = nullptr);
int StepEnemyWalls(const std::vector<Wall>& walls, const std::vector<Wall>& enemyOnly,
                   WallPoint from, WallPoint& to);
int StepZombieWorld(WorldActor& actor, const std::vector<WorldActor>& ordered,
                    const WorldActor* player, const std::vector<Wall>& walls,
                    const std::vector<Wall>& enemyOnly);

}  // namespace ps1
