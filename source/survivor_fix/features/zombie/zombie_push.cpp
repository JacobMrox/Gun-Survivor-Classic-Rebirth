// PS1 separation between zombie and player (80081618, 80081C80).
#include "zombie_shared.h"

namespace sf::features::zombie {

// Ps1ZombiePush -------------------------------------------------------------------
// 80081C80/80081FF0: push of the player out of the zombie's 600 + 380 circle, only in the updates in
// which the player moved (on the PS1 it is part of their step).
// ASSUMPTION: "moved" = position changed since this zombie's last update.
// ASSUMPTION: the player's PC y is 1600 above the feet (spawn: player -1600, zombie 0 on the same floor),
// so the +/-800 check uses that offset.
// On the PS1 the push then goes through the walls (8007CB9C, sliding); here, if the movement crosses a
// ROOMENV wall, it is not applied (TODO: sliding).
struct PushEntry {
  float px = 0.0f, pz = 0.0f;
  bool valid = false;
};
std::unordered_map<void*, PushEntry> g_pushes;

void PushPlayer(void* s) {
  if (!g_config.ps1ZombiePush || !ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) return;
  if (PlayerHeld()) return;  // player grabbed or bitten: controls blocked
  // 80081C80 considers only the active enemies (E+2A == 1, E+29 != 0). ASSUMPTION: on the PC the zombie
  // is active when it walks or attacks (not with request 01, when it sleeps).
  const int request = game::Read<int>(s, game::S::kRequest);
  if (!IsLocomotion(request) && request != kAttackRequest) return;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return;
  if (WorldOwnsObject(o)) return;
  float* pp = game::Field<float>(t, game::O::kPos);
  const float* op = reinterpret_cast<const float*>(o + game::O::kPos);
  PushEntry& e = g_pushes[s];
  const bool moved = e.valid && (pp[0] != e.px || pp[2] != e.pz);
  e.valid = true;
  e.px = pp[0];
  e.pz = pp[2];
  if (!moved) return;
  const float dy = (pp[1] + kPlayerEyeHeight) - op[1];
  const ps1::Vec2 push = ps1::PlayerPush({pp[0], pp[2]}, {op[0], op[2]}, dy, ps1::kZombieRadius);
  if (push.x == 0.0f && push.z == 0.0f) return;
  const ps1::Vec2 from{pp[0], -pp[2]}, to{pp[0] + push.x, -(pp[2] + push.z)};
  const auto& walls = CurrentRoomWalls();
  if (!walls.empty() && ps1::FirstBlockingWall(walls, from, to) >= 0) return;
  pp[0] += push.x;
  pp[2] += push.z;
  e.px = pp[0];
  e.pz = pp[2];
}

// 80081618: on every frame in which the zombie has velocity (E+87C/880/884 != 0, and not in
// 0x200000/0x8000), after the other enemies, 80081FF0(player, zombie) with radius 600 + E+8C0 adds the
// push to the zombie's velocity: the zombie does not enter the player's 980 circle (on the PS1 it stops
// in front at a constant distance). ASSUMPTION: "has velocity" = moved since the last update.
// TODO: the body parts (E+392) with E+2B & 4.
struct SeparationEntry {
  float x = 0.0f, z = 0.0f;
  bool valid = false;
};
std::unordered_map<void*, SeparationEntry> g_separations;

void SeparateZombie(void* s) {
  if (!g_config.ps1ZombiePush || !ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) return;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return;
  if (WorldOwnsObject(o)) return;
  const float* zp = reinterpret_cast<const float*>(o + game::O::kPos);
  const float* pp = reinterpret_cast<const float*>(t + game::O::kPos);
  SeparationEntry& e = g_separations[s];
  // 80103868 substates 3-6: position set in front of the player, velocity 0.
  auto phase = g_entries.find(s);
  const bool pinned = phase != g_entries.end() &&
                      (phase->second.phase == Phase::LungeGrab || phase->second.phase == Phase::LungeBite);
  const bool moving = e.valid && !pinned && (zp[0] != e.x || zp[2] != e.z);
  e.valid = true;
  if (moving) {
    const float dy = zp[1] - (pp[1] + kPlayerEyeHeight);
    const ps1::Vec2 push = ps1::PlayerPush({zp[0], zp[2]}, {pp[0], pp[2]}, dy, ps1::kZombieRadius);
    if (push.x != 0.0f || push.z != 0.0f) MoveZombie(o, push.x, push.z);
  }
  e.x = zp[0];
  e.z = zp[2];
}

void ResetPushActor(void* s) {
  g_pushes.erase(s);
  g_separations.erase(s);
}

void ResetPushes() {
  g_pushes.clear();
  g_separations.clear();
}

}  // namespace sf::features::zombie
