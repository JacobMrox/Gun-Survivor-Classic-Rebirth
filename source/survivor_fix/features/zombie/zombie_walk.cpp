// Zombie walk (Ps1ZombieWalk), PS1 walk speed (Ps1ZombieSpeed) and walk animation speed
// (Ps1ZombieWalkAnim).
#include "zombie_shared.h"

namespace sf::features::zombie {

// PS1 801047B4 chooses P+4 once at initialisation. 80104D5C and 80104E78 reuse it
// through bites, falls and recovery; elapsed time never identifies a new life.
ps1::WalkSpeedLifetime g_walkSpeeds;

void InitializeWalkBase(void* s) {
  g_walkSpeeds.Initialize(reinterpret_cast<std::uintptr_t>(s), NextRngByte());
}

// PC 4361B0 -> 436B04 CALL RequestAnim, return 436B09.
// Invalidated even while the feature is off: a reused S must not inherit an old life.
void BeginWalkSpeedLife(void* s) {
  g_walkSpeeds.Forget(reinterpret_cast<std::uintptr_t>(s));
  if (g_config.ps1ZombieSpeed || g_config.ps1ZombieBite) InitializeWalkBase(s);
}

int WalkBase(void* s) {
  const auto actor = reinterpret_cast<std::uintptr_t>(s);
  // Late attachment or a life initialised while off: chosen on the first active use.
  if (g_walkSpeeds.Base(actor) == 0) InitializeWalkBase(s);
  return g_walkSpeeds.Base(actor);
}

void ResetWalkRoom() { g_walkSpeeds = ps1::WalkSpeedLifetime(); }

// Ps1ZombieWalk: one walk only, like on the PS1.
// The PS1 chooses the locomotion when entering the chase (801019F0 / 80101D44: 80101CE4(E, rng & 1 | 2),
// i.e. 02 or 03 with the preset of the first zombie) and repeats it; the end is checked on track 1
// (80100728(E,1,1)). For type 0 the PC draws (R & 3) + 1 at every "completion", which 0043A8A0 measures on
// track 0: in walks 01 and 02 that track loops in 4 ticks (against 120/124 of the others), so the game
// chooses another one after a few ticks and starts again from the initial pose: the jerk. Here the mapper
// always returns the current walk (the tracks loop by themselves) and, entering the walk, chooses 02 or 03.
int PickWalk(void* s, int request, int result) {
  const int type = game::Read<int>(s, game::S::kType);
  if (!IsLocomotion(request) || !ps1::IsZombieWalk(type, result)) return result;
  if (!ps1::IsPcZombie(type)) return result;
  auto it = g_entries.find(s);
  if (it != g_entries.end() && it->second.phase != Phase::Idle) return result;
  // 80103FA4: in the grab from behind the zombie uses animation 4 (80080334(E,4,2)).
  if (g_config.ps1ZombieBite && GrabActiveFor(s)) return 4;
  // 80102828: below 0x5DD from the player, once, rand < 0x3F (25%) -> P+0C = 1 and from walk 1-3 it
  // goes on to 4, arms forward towards the player (80080268(E, 4), blend 0x180). DuckStation 2026-10-22:
  // 2 -> 4 at d 1445-1490 in every recording. The special walks 26..29 stay (TODO: check 4 on types 9
  // and 10).
  if (g_config.ps1ZombieWalk && !ps1::SpecialZombieWalk(type)) {
    uint8_t *o, *t;
    Entry& e = g_entries[s];
    if (GetObjects(s, o, t) && Measure(o, t).distance < ps1::kArmsDistance) {
      if (e.armsRoll == 0) e.armsRoll = NextRngByte() < ps1::kArmsRng ? 1 : 2;
      const int now = game::Read<int>(s, game::S::kSelector);
      if (e.armsRoll == 1 && now >= 1 && now <= 3) {
        e.rememberedWalk = 4;
        RequestPs1Blend(s, ps1::kBlendStateChange);
        return 4;
      }
    }
  }
  if (it != g_entries.end() && it->second.forceWalk) {
    int pick = ps1::SpecialZombieWalk(type) && ps1::IsZombieWalk(type, it->second.rememberedWalk)
                   ? it->second.rememberedWalk
                   : (ps1::SpecialZombieWalk(type) ? result : it->second.forceWalk);
    if (pick == 4 && it->second.armsRoll != 1) pick = 2 + (NextRngByte() & 1);  // 4 only with the roll of 80102828
    it->second.forceWalk = 0;
    RequestPs1Blend(s, ps1::kBlendStateChange);  // 800802C8(E,4,walk): 24 frames
    return pick;
  }
  if (!g_config.ps1ZombieWalk) return result;
  const int current = game::Read<int>(s, game::S::kSelector);
  // The PS1 sets 4 (arms forward) only with the roll of 80102828 or in the grab from behind: the one the
  // PC chooses by itself ((R & 3) + 1 on waking up and getting up) is not kept.
  if (ps1::IsZombieWalk(type, current) &&
      !(current == 4 && !ps1::SpecialZombieWalk(type) && g_entries[s].armsRoll != 1)) {
    g_entries[s].rememberedWalk = current;
    return current;
  }
  // 80101CE4 keeps a preset locomotion such as 26..29; it is not replaced with 02/03.
  const int pick = ps1::SpecialZombieWalk(type)
                       ? (ps1::IsZombieWalk(type, g_entries[s].rememberedWalk) ? g_entries[s].rememberedWalk : result)
                       : 2 + (NextRngByte() & 1);
  g_entries[s].rememberedWalk = pick;
  return pick;
}

int ApplyWalkHold(void* s, int request, int result) {
  const int walk = PickWalk(s, request, result);
  // Ps1ZombieWalkAnim (80104E78): every walk starts at (n+1) times the normal speed by the base. S+40
  // is the proposed speed, which the game applies only when the clip starts or restarts: writing it at
  // every request does not change the clip already running.
  if (g_config.ps1ZombieWalkAnim && g_config.ps1ZombieSpeed && IsLocomotion(request) && ps1::IsPs1WalkClip(walk) &&
      ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) {
    *game::Field<float>(s, game::S::kNextSpeed) = g_config.walkAnimSpeed * ps1::WalkAnimMultiplier(WalkBase(s));
  }
  return walk;
}

// PC walk of the zombie measured in game (room 7/0): 12.3 units per update at ~29.5 updates/s, at any
// distance. Scaling O+D4 by k the measured movement is ~6.2 + 5.74 k units per update (one zombie from
// 1500 to 16000 away, line over 4 targets): about half of the step does not go through O+D4 after the
// update (damping O+C8 x0.8 in 0045E730 and other contributions). The line is inverted.
// ASSUMPTION: the line holds for both walks 02 and 03.
constexpr float kPcWalkFixed = 6.2f;
constexpr float kPcWalkPerScale = 5.74f;

// Scales the movement accumulated by the walk in this update. The enemy logic accumulates (0,0,S+74) in O+D4;
// the real movement is applied after the update (that is why clearing it during the falls stops the
// zombie). The PS1 works per frame at 30/s, the PC per update at ~29.5/s: units per PS1 frame = units
// per PC update.
void ScaleWalk(void* s) {
  if (!g_config.ps1ZombieSpeed || !ps1::IsPcZombie(game::Read<int>(s, game::S::kType)) ||
      !IsLocomotion(game::Read<int>(s, game::S::kRequest))) {
    return;
  }
  auto it = g_entries.find(s);
  if (it != g_entries.end() && it->second.phase != Phase::Idle) return;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return;
  if (WorldOwnsObject(o)) return;
  const int target = ps1::WalkSpeedForDistance(WalkBase(s), static_cast<int>(Measure(o, t).distance));
  const float wanted = static_cast<float>(target);
  const float scale = wanted > kPcWalkFixed ? (wanted - kPcWalkFixed) / kPcWalkPerScale : 0.0f;
  float* accum = game::Field<float>(o, game::O::kAccum);
  accum[0] *= scale;
  accum[2] *= scale;
}

// PS1 speed of the zombie in this frame (E+866): Ps1ZombieSpeed base for the distance (80104D5C).
int ZombieWalkSpeed(void* s, float distance) {
  return ps1::WalkSpeedForDistance(WalkBase(s), static_cast<int>(distance));
}

}  // namespace sf::features::zombie
