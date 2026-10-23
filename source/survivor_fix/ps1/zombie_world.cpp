#include "zombie_world.h"

#include <cmath>
#include <limits>

namespace ps1 {

namespace {

int32_t Add(int32_t a, int32_t b) { return int32_t(uint32_t(a) + uint32_t(b)); }
int64_t Magnitude(int32_t a) { return a < 0 ? -int64_t(a) : int64_t(a); }

int32_t Reduce(int32_t velocity, int32_t push, int32_t towards) {
  const int32_t sum = Add(velocity, push);
  if (Magnitude(sum) < Magnitude(velocity)) return sum;
  if (Magnitude(towards) < Magnitude(Add(towards, velocity))) {
    int32_t v = int32_t(Magnitude(velocity) >> 3);
    return velocity < 0 ? -v : v;
  }
  return velocity;
}

}  // namespace

WorldVector ContactPush(const WorldActor& fixed, const WorldActor& moving) {
  // 80081FF0: integer distance; strict vertical gate; no Y correction.
  const int64_t radius = int64_t(fixed.radius) + moving.radius;
  if (radius <= 0 || radius > (std::numeric_limits<int32_t>::max)()) return {};
  const int64_t dx = int64_t(moving.position.x) - fixed.position.x;
  const int64_t dz = int64_t(moving.position.z) - fixed.position.z;
  const int64_t dy = int64_t(moving.position.y) - fixed.position.y;
  if (dx < -radius || dx > radius || dz < -radius || dz > radius || dy <= -800 || dy >= 800) return {};
  const int64_t distance = int64_t(std::sqrt(double(dx * dx + dz * dz)));
  const int64_t depth = radius - distance;
  if (depth <= 0) return {};
  return {int32_t(dx * depth / (distance + 1)), 0, int32_t(dz * depth / (distance + 1))};
}

WorldVector PlayerContactPush(const WorldActor& player, const std::vector<WorldActor>& ordered) {
  WorldVector sum;
  for (const auto& other : ordered) {
    if (!other.active || (other.flags & (0x8000 | 1))) continue;
    const auto push = ContactPush(other, player);
    sum.x = Add(sum.x, push.x);
    sum.z = Add(sum.z, push.z);
  }
  return sum;
}

void ResolveActorContacts(WorldActor& actor, const std::vector<WorldActor>& ordered,
                          const WorldActor* player) {
  // 80081618 gates and iteration order. Native flags, not guessed clip names.
  if (!actor.active || (actor.flags & (0x200000 | 0x8000)) ||
      (!actor.velocity.x && !actor.velocity.y && !actor.velocity.z)) return;
  for (const auto& other : ordered) {
    if (other.life == actor.life || !other.active || (other.flags & (0x8000 | 1))) continue;
    const auto push = ContactPush(other, actor);
    if (!push.x && !push.z) continue;
    actor.velocity.x = Reduce(actor.velocity.x, push.x,
                              int32_t(uint32_t(other.position.x) - uint32_t(actor.position.x)));
    actor.velocity.z = Reduce(actor.velocity.z, push.z,
                              int32_t(uint32_t(other.position.z) - uint32_t(actor.position.z)));
  }
  if (player) {
    const auto push = ContactPush(*player, actor);
    if (push.x || push.z) {
      actor.flags |= 0x80000;
      actor.velocity.x = Add(actor.velocity.x, push.x);
      actor.velocity.z = Add(actor.velocity.z, push.z);
    } else {
      actor.flags &= ~0x80000u;
    }
  }
}

int StepEnemyWalls(const std::vector<Wall>& walls, const std::vector<Wall>& enemyOnly,
                   WallPoint from, WallPoint& to) {
  return StepAgainstEnemyWalls(walls, enemyOnly, from, to);
}

int StepZombieWorld(WorldActor& actor, const std::vector<WorldActor>& ordered,
                    const WorldActor* player, const std::vector<Wall>& walls,
                    const std::vector<Wall>& enemyOnly) {
  ResolveActorContacts(actor, ordered, player);
  int hits = 0;
  if (!(actor.flags & 0x100)) {
    WallPoint to{Add(actor.position.x, actor.velocity.x), Add(actor.position.z, actor.velocity.z)};
    hits = StepEnemyWalls(walls, enemyOnly, {actor.position.x, actor.position.z}, to);
    actor.position.x = to.x;
    actor.position.z = to.z;
    actor.position.y = Add(actor.position.y, actor.velocity.y);
    if (hits)
      actor.flags |= 0x20;
    else
      actor.flags &= ~0x20u;
  }
  actor.velocity = {};  // 8007F404 clears all three components after commit
  return hits;
}

}  // namespace ps1
