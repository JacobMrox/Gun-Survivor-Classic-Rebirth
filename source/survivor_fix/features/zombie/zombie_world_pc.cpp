// Shared PS1 planar world for the zombies. MIPS order 8007FEBC -> 80081618 -> 8007F404.
// The native PC physics phase keeps the vertical/static support; the owned XZ is committed once
// after the real gameplay enemy logic call. No wall-clock lifetime or guessed hook ABI.
#include "zombie_shared.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace sf::features::zombie {

namespace {

struct ActorLife {
  void* s = nullptr;
  uint8_t* c = nullptr;
  uint8_t* o = nullptr;
  uint8_t* target = nullptr;
  uint8_t* model = nullptr;
  uint64_t order = 0;
  int index = -1, slot = -1;
  uint32_t tickBefore = 0;
  bool inUpdate = false, walkPlanned = false, directPlanned = false;
  float dx = 0, dz = 0;
};
std::unordered_map<void*, ActorLife> lives;
uint64_t nextOrder = 0;
bool bridgeReady = false;
struct NativeSaved {
  uint8_t* o;
  uint32_t flags;
  uint8_t orient;
  float x, z;
  bool owned;
};
std::vector<NativeSaved> nativeSaved;
void* physicsCollection = nullptr;
ps1::WorldVector playerBefore;
bool havePlayerBefore = false;

bool ValidModel(const uint8_t* o, int type) {
  auto* m = game::Read<uint8_t*>(o, game::O::kModel);
  if (!m) return false;
  const unsigned count = game::Read<unsigned>(m, game::M::kTrackCount);
  const auto* tracks = game::Read<uint8_t*>(m, game::M::kTracks);
  if (!tracks || count < 2 || count > 64) return false;
  for (unsigned i = 0; i < count; ++i)
    if (game::Read<int>(tracks + i * game::M::kTrackSize, 0xC) < ps1::RequiredZombieSequences(type)) return false;
  return true;
}

ActorLife* FindObject(const uint8_t* o) {
  for (auto& entry : lives)
    if (entry.second.o == o) return &entry.second;
  return nullptr;
}

bool StillBound(const ActorLife& life, void* c) {
  if (life.c != c) return false;
  const int count = game::Read<int>(c, game::C::kCount);
  auto** objects = game::Read<uint8_t**>(c, game::C::kObjects);
  const int target = game::Read<int>(c, game::C::kTarget);
  return objects && life.index >= 0 && life.index < count && target >= 0 && target < count &&
         objects[life.index] == life.o && objects[target] == life.target && life.o != life.target &&
         game::Read<uint8_t*>(life.o, game::O::kModel) == life.model;
}

bool CorpseWithoutContacts(const ActorLife& life) {
  // PS1 801032CC substate 3 sets E+320 mask 1 when entering the final ground phase, before 12/13
  // finishes; 80081C80/80081618 then skip this body. Health alone is not enough.
  // PC 436E40 applies 0F/12 or 10/13 with S+2C = 50 after the fall completes; later 09/0D use
  // S+4C = 1. The applied byte S+2C is read, not the proposed S+3C or the adjacent done byte S+2D.
  // The native 50 is a transition marker, never a timer introduced by this adapter.
  const float hp = game::Read<float>(life.o, 0x11C);
  if (!ps1::IsPcZombie(game::Read<int>(life.s, game::S::kType)) || !std::isfinite(hp) || hp > 0) return false;
  if (DeathLying(life.s)) return true;  // Ps1ZombieDeath: PS1 pose on the ground (801032CC substate 3)
  const int request = game::Read<int>(life.s, game::S::kRequest);
  const int selector = game::Read<int>(life.s, game::S::kSelector);
  const bool ground = (request == 0x0F && selector == 0x12) || (request == 0x10 && selector == 0x13);
  if (ground && game::Read<uint8_t>(life.s, game::S::kThreshold) == 0x32) return true;
  // The request can change without applying a pose again (436B40); a type 01 kitchen zombie went
  // 0F/09 -> 10/09 keeping HP 0/S+4C 1. The applied final pose defines the body, not a pending request.
  // No sticky lifetime flag: living/reused actors still check the health.
  const bool final = selector == 9 || selector == 0x0D;
  return final && game::Read<int>(life.s, 0x4C) == 1;
}

bool Owns(const ActorLife& life) {
  if (!StillBound(life, life.c) || CorpseWithoutContacts(life)) return false;
  const int request = game::Read<int>(life.s, game::S::kRequest);
  const auto phase = g_entries.find(life.s);
  return game::Read<uint8_t>(life.s, 0x68) != 0 &&
         (IsLocomotion(request) || request == kAttackRequest || (phase != g_entries.end() && phase->second.phase != Phase::Idle));
}

bool IntCoordinate(float v, int32_t& out) {
  if (!std::isfinite(v) || double(v) < (std::numeric_limits<int32_t>::min)() ||
      double(v) > (std::numeric_limits<int32_t>::max)())
    return false;
  out = static_cast<int32_t>(v);
  return true;
}

bool Position(const uint8_t* o, ps1::WorldVector& p) {
  const auto* v = reinterpret_cast<const float*>(o + game::O::kPos);
  return IntCoordinate(v[0], p.x) && IntCoordinate(v[1], p.y) && IntCoordinate(-v[2], p.z);
}

ps1::WorldActor Body(const ActorLife& life) {
  ps1::WorldActor body;
  body.life = static_cast<uintptr_t>(life.order);
  body.active = Position(life.o, body.position) && game::Read<uint8_t>(life.s, 0x68) != 0;
  // The native exclusion is mapped explicitly; raw PC flags are never PS1 flags.
  const uint32_t flags = game::Read<uint32_t>(life.o, game::O::kCollisionFlags);
  bool originallyExcluded = (flags & 2) != 0;
  for (const auto& saved : nativeSaved)
    if (saved.o == life.o) originallyExcluded = (saved.flags & 2) != 0;
  if (originallyExcluded) body.flags |= 0x8000;
  if (CorpseWithoutContacts(life)) body.flags |= 1;
  return body;
}

std::vector<ps1::WorldActor> Bodies(void* c) {
  std::vector<const ActorLife*> sorted;
  for (const auto& item : lives)
    if (StillBound(item.second, c)) sorted.push_back(&item.second);
  std::sort(sorted.begin(), sorted.end(), [](const ActorLife* a, const ActorLife* b) { return a->slot < b->slot; });
  std::vector<ps1::WorldActor> result;
  result.reserve(sorted.size());
  for (const auto* life : sorted) result.push_back(Body(*life));
  return result;
}

void ClearPlanar(uint8_t* o) {
  auto* v = game::Field<float>(o, 0xC8);
  auto* a = game::Field<float>(o, game::O::kAccum);
  v[0] = v[2] = a[0] = a[2] = 0;
}

}  // namespace

bool WorldEnabled() {
  return bridgeReady && g_config.ps1ZombieWorld && g_config.ps1ZombieNavigation && g_config.ps1ZombieSpeed &&
         g_config.ps1ZombiePush && CurrentRoomGeometryReady();
}

void WorldBridgeReady(bool ready) { bridgeReady = ready; }

bool WorldOwnsObject(const uint8_t* o) {
  if (!o || !WorldEnabled()) return false;
  auto* life = FindObject(o);
  return life && Owns(*life);
}

void SyncObjectPose(uint8_t* o) {
  // Same affine invariant as 0045E110: pos = pivot*matrix + translation,
  // aimPoint = (aimLocal+pivot)*matrix + translation. No independent stale matrix position.
  const auto* pos = reinterpret_cast<const float*>(o + game::O::kPos);
  const auto* pivot = reinterpret_cast<const float*>(o + game::O::kPivot);
  const auto* local = reinterpret_cast<const float*>(o + game::O::kAimOffsetLocal);
  auto* m = game::Field<float>(o, game::O::kMatrix);
  auto* aim = game::Field<float>(o, game::O::kAimPoint);
  for (int k = 0; k < 3; ++k) {
    m[12 + k] = pos[k] - (pivot[0] * m[k] + pivot[1] * m[4 + k] + pivot[2] * m[8 + k]);
    aim[k] = (local[0] + pivot[0]) * m[k] + (local[1] + pivot[1]) * m[4 + k] + (local[2] + pivot[2]) * m[8 + k] + m[12 + k];
  }
}

void NativeZombieLife(void* s) {
  // The native initialisation is authoritative even if the PS1 world is off.
  g_entries.erase(s);
  ResetBiteActor(s);
  ResetPushActor(s);
  ResetDeathActor(s);
  lives.erase(s);
  if (!ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) return;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t) || !ValidModel(o, game::Read<int>(s, game::S::kType))) return;
  for (auto it = lives.begin(); it != lives.end();) {
    if (it->second.o == o) {
      g_entries.erase(it->first);
      ResetBiteActor(it->first);
      ResetPushActor(it->first);
      ResetDeathActor(it->first);
      it = lives.erase(it);
    } else {
      ++it;
    }
  }
  ActorLife life;
  life.s = s;
  life.c = game::Read<uint8_t*>(s, game::S::kCollection);
  life.o = o;
  life.target = t;
  life.model = game::Read<uint8_t*>(o, game::O::kModel);
  life.index = game::Read<int>(s, game::S::kIndex);
  life.slot = game::Read<int>(s, game::S::kAiSlot);
  // 0043C600 bounds the native manager at 30 slots; an unverified ordering is never used.
  if (life.slot < 0 || life.slot >= 30) return;
  life.order = ++nextOrder;
  lives.emplace(s, life);
}

void ResetZombieRoom() {
  lives.clear();
  g_entries.clear();
  ResetBites();
  ResetPushes();
  ResetWalkRoom();
  ResetDeaths();
  // nativeSaved is not destroyed here: an in-flight native phase still owns its restoration.
}

bool WorldBeginUpdate(void* s, uintptr_t caller) {
  auto it = lives.find(s);
  if (it == lives.end()) return false;
  auto& life = it->second;
  life.inUpdate = false;
  life.walkPlanned = life.directPlanned = false;
  if (!WorldEnabled() || caller != game::kWorldEnemyUpdateReturn || !StillBound(life, life.c)) return false;
  life.inUpdate = true;
  life.tickBefore = game::Read<uint32_t>(s, 0x44);
  return true;
}

void WorldPlanWalk(void* s, float fx, float fz, bool walk) {
  auto it = lives.find(s);
  if (it == lives.end() || !it->second.inUpdate) return;
  auto& life = it->second;
  ps1::WorldVector z, p;
  if (!Position(life.o, z) || !Position(life.target, p)) return;
  const double dx = double(p.x) - z.x, dz = double(p.z) - z.z;
  const int speed = walk ? ZombieWalkSpeed(s, float(std::sqrt(dx * dx + dz * dz))) : 0;
  // 8007F210 -> 8008286C rounds the vector, unlike the world-position truncation.
  // Rounded in PS1 axes: reflecting Z afterwards preserves negative half-ties.
  // The existing PC float heading is kept; SDK/GTE matrix equivalence is unverified.
  int32_t vx = 0, vz = 0;
  life.walkPlanned = ps1::RoundWalkComponent(fx * speed, vx) && ps1::RoundWalkComponent(-fz * speed, vz);
  life.dx = float(vx);
  life.dz = -float(vz);
}

bool WorldMove(uint8_t* o, float dx, float dz) {
  if (!WorldEnabled()) return false;
  auto* life = FindObject(o);
  if (!life || !life->inUpdate) return false;
  if (!std::isfinite(dx) || !std::isfinite(dz)) return true;
  life->dx = dx;
  life->dz = dz;
  life->directPlanned = true;
  return true;
}

void WorldFinishUpdate(void* s) {
  auto it = lives.find(s);
  if (it == lives.end()) return;
  auto& life = it->second;
  if (!life.inUpdate) return;
  life.inUpdate = false;
  if (!WorldEnabled() || game::Read<uint32_t>(s, 0x44) == life.tickBefore) return;
  auto phase = g_entries.find(s);
  const bool pinned = phase != g_entries.end() &&
                      (phase->second.phase == Phase::LungeGrab || phase->second.phase == Phase::LungeBite);
  const bool ordinary = phase == g_entries.end() || phase->second.phase == Phase::Idle;
  const bool move = !pinned && (life.directPlanned ||
                                (life.walkPlanned && ordinary && IsLocomotion(game::Read<int>(s, game::S::kRequest))));
  // 80104108 LAB_801042A8: while the new clip blends in, E+87C/E+884 = 0 (contacts still run).
  const bool still = phase != g_entries.end() && phase->second.stillUpdates > 0;
  if (still) --phase->second.stillUpdates;
  if (move) {
    auto actor = Body(life);
    int32_t vx = 0, vz = 0;
    if (still) life.dx = life.dz = 0.0f;
    // 8007FEBC does not integrate a body after its terminal mask 1 is set.
    if (actor.active && !(actor.flags & 1) && IntCoordinate(life.dx, vx) && IntCoordinate(-life.dz, vz)) {
      actor.velocity = {vx, 0, vz};
      ps1::WorldActor player;
      player.radius = 600;
      if (Position(life.target, player.position)) {
        // Existing verified PC eye-origin conversion, kept as an adapter assumption.
        player.position.y += static_cast<int32_t>(kPlayerEyeHeight);
        const auto ordered = Bodies(life.c);
        ps1::StepZombieWorld(actor, ordered, &player, CurrentRoomWalls(), CurrentEnemyOnlyWalls());
        auto* pos = game::Field<float>(life.o, game::O::kPos);
        pos[0] = float(actor.position.x);
        pos[2] = -float(actor.position.z);
      }
    }
  }
  if (WorldOwnsObject(life.o)) ClearPlanar(life.o);
  SyncObjectPose(life.o);
}

void WorldPhysicsBegin(void* c) {
  if (!WorldEnabled() || !c || !nativeSaved.empty()) return;
  physicsCollection = c;
  auto** playerObjects = game::Read<uint8_t**>(c, game::C::kObjects);
  const int playerIndex = game::Read<int>(c, game::C::kTarget), objectCount = game::Read<int>(c, game::C::kCount);
  havePlayerBefore = playerObjects && playerIndex >= 0 && playerIndex < objectCount && playerObjects[playerIndex] &&
                     Position(playerObjects[playerIndex], playerBefore);
  for (auto& item : lives) {
    auto& life = item.second;
    if (!StillBound(life, c)) continue;
    auto* o = life.o;
    const auto* p = reinterpret_cast<const float*>(o + game::O::kPos);
    const bool owned = Owns(life);
    nativeSaved.push_back({o, game::Read<uint32_t>(o, game::O::kCollisionFlags), game::Read<uint8_t>(o, 0x10E), p[0],
                           p[2], owned});
    if (owned) {
      ClearPlanar(o);
      *game::Field<uint8_t>(o, 0x10E) = 0;
    }
  }
}

void WorldBeforeQuery(void* c, int index) {
  if (c != physicsCollection || nativeSaved.empty()) return;
  auto** objects = game::Read<uint8_t**>(c, game::C::kObjects);
  const int count = game::Read<int>(c, game::C::kCount);
  const bool common = objects && index >= 0 && index < count &&
                      (WorldOwnsObject(objects[index]) || index == game::Read<int>(c, game::C::kTarget));
  // This mask remains through the native support probe for the same moving actor.
  // The next query restores the original participation for unrelated native families.
  for (const auto& save : nativeSaved) {
    auto* flags = game::Field<uint32_t>(save.o, game::O::kCollisionFlags);
    // A corpse is no obstacle even to an actor still using native collision (e.g. a living fallen
    // zombie). Living obstacles keep their original participation.
    const auto* life = FindObject(save.o);
    const bool corpse = life && CorpseWithoutContacts(*life);
    *flags = (*flags & ~2u) | (save.flags & 2u) | ((common || corpse) ? 2u : 0u);
  }
}

void WorldPhysicsEnd(void* c) {
  if (c != physicsCollection) return;
  for (const auto& save : nativeSaved) {
    auto* flags = game::Field<uint32_t>(save.o, game::O::kCollisionFlags);
    *flags = (*flags & ~2u) | (save.flags & 2u);
    *game::Field<uint8_t>(save.o, 0x10E) = save.orient;
    if (save.owned) {
      auto* p = game::Field<float>(save.o, game::O::kPos);
      p[0] = save.x;
      p[2] = save.z;
      ClearPlanar(save.o);
      SyncObjectPose(save.o);
    }
  }
  if (havePlayerBefore && !PlayerHeld()) {
    auto** objects = game::Read<uint8_t**>(c, game::C::kObjects);
    const int target = game::Read<int>(c, game::C::kTarget);
    if (objects && target >= 0 && target < game::Read<int>(c, game::C::kCount) && objects[target]) {
      auto* playerObject = objects[target];
      ps1::WorldActor player;
      player.radius = 600;
      if (Position(playerObject, player.position) &&
          (player.position.x != playerBefore.x || player.position.z != playerBefore.z)) {
        player.position.y += static_cast<int32_t>(kPlayerEyeHeight);
        const auto push = ps1::PlayerContactPush(player, Bodies(c));
        if (push.x || push.z) {
          ps1::WallPoint next{player.position.x + push.x, player.position.z + push.z};
          ps1::StepAgainstWalls(CurrentRoomWalls(), {player.position.x, player.position.z}, next);
          auto* pos = game::Field<float>(playerObject, game::O::kPos);
          pos[0] = float(next.x);
          pos[2] = -float(next.z);
          SyncObjectPose(playerObject);
        }
      }
    }
  }
  nativeSaved.clear();
  physicsCollection = nullptr;
  havePlayerBefore = false;
}

}  // namespace sf::features::zombie
