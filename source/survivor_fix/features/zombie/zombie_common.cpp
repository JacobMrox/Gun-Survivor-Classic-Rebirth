// Zombies: common state, mapper (00436E40) and update (0043A8A0) detours, installation.
// The fixes live in the other zombie_*.cpp files.
#include <intrin.h>

#include "zombie_shared.h"

namespace sf::features::zombie {

MapAnimFn g_mapAnim = nullptr;
UpdateEnemyFn g_updateEnemy = nullptr;

std::unordered_map<void*, Entry> g_entries;
uint32_t g_rng = 0;

int NextRngByte() {
  g_rng = g_rng * 0x015A4E35u + 1u;  // the same LCG as the game (00407230)
  return static_cast<int>((g_rng >> 16) & 0xFF);
}

bool IsLocomotion(int request) { return request >= 2 && request <= 4; }
// 0043A8A0 with O+11C (health) <= 0 asks for 0x10 or 0x0F.
bool IsDeath(int request) { return request == 0x10 || request == 0x0F; }
bool IsBackChain(Phase phase) {
  return phase == Phase::BackFall || phase == Phase::Supine || phase == Phase::SitUp;
}

bool IsLungePhase(Phase phase) {
  return phase == Phase::Lunge || phase == Phase::LungeMiss || phase == Phase::LungeGrab ||
         phase == Phase::LungeBite || phase == Phase::Knockback;
}

float PhaseSpeed(Phase phase) {
  // Lunge and 0x20: on the PS1 at 1x (80080334/8008035c, track +0xD = 0x10), like the 0x1B
  // of the bite: same conversion (BiteSpeed).
  if (IsLungePhase(phase) || phase == Phase::Death || phase == Phase::Feed) {
    return g_config.biteSpeed;  // 801032CC, 800802C8: 1x
  }
  switch (phase) {
    case Phase::Fall: return g_config.fallSpeed;
    case Phase::GetUp1: return g_config.getUp1Speed;
    case Phase::GetUp2: return g_config.getUp2Speed;
    case Phase::BackFall: return g_config.backFallSpeed;
    case Phase::Supine: return g_config.supineSpeed;
    default: return g_config.sitUpSpeed;
  }
}

int PhaseSelector(Phase phase) {
  switch (phase) {
    case Phase::Lunge: return ps1::kSelLunge;
    case Phase::LungeMiss: return ps1::kSelLungeMiss;
    case Phase::LungeGrab: return ps1::kSelLungeGrab;
    case Phase::LungeBite: return ps1::kSelLungeBite;
    case Phase::Knockback: return ps1::kSelKnockback;
    case Phase::Fall: return ps1::kSelFall;
    case Phase::GetUp1: return ps1::kSelGetUp1;
    case Phase::GetUp2: return ps1::kSelGetUp2;
    case Phase::BackFall: return ps1::kSelBackFall;
    case Phase::Supine: return ps1::kSelSupine;
    case Phase::SitUp: return ps1::kSelSitUp;
    default: return -1;
  }
}

// O and T of the zombie, or false if the pointer chain is not valid.
bool GetObjects(void* s, uint8_t*& o, uint8_t*& t) {
  auto* c = game::Read<uint8_t*>(s, game::S::kCollection);
  if (!c) return false;
  auto** objects = game::Read<uint8_t**>(c, game::C::kObjects);
  if (!objects) return false;
  o = objects[game::Read<int>(s, game::S::kIndex)];
  t = objects[game::Read<int>(c, game::C::kTarget)];
  return o && t && o != t;
}

Geometry Measure(const uint8_t* o, const uint8_t* t) {
  const float* op = reinterpret_cast<const float*>(o + game::O::kPos);
  const float* tp = reinterpret_cast<const float*>(t + game::O::kPos);
  Geometry g;
  g.toX = tp[0] - op[0];
  g.toZ = tp[2] - op[2];
  g.distance = std::sqrt(g.toX * g.toX + g.toZ * g.toZ);
  return g;
}

bool InSectorOf(const uint8_t* viewer, float toX, float toZ) {
  const float* f = reinterpret_cast<const float*>(viewer + game::O::kForward);
  return ps1::InFrontSector(f[0], f[2], toX, toZ);
}

float AngleDegrees(const uint8_t* viewer, float toX, float toZ) {
  const float* f = reinterpret_cast<const float*>(viewer + game::O::kForward);
  const float len = std::sqrt((f[0] * f[0] + f[2] * f[2]) * (toX * toX + toZ * toZ));
  if (len <= 0.0f) return -1.0f;
  return std::acos((f[0] * toX + f[2] * toZ) / len) * 180.0f / 3.14159265f;
}

// Track Q used for the end of clip: track 1 like on the PS1 (Q1), otherwise track 0.
uint8_t* ExitTrack(const uint8_t* o) {
  auto* m = game::Read<uint8_t*>(o, game::O::kModel);
  if (!m) return nullptr;
  auto* tracks = game::Read<uint8_t*>(m, game::M::kTracks);
  const auto count = game::Read<unsigned>(m, game::M::kTrackCount);
  if (!tracks || count == 0) return nullptr;
  return tracks + (count > 1 ? game::M::kTrackSize : 0);
}

int TrackStartCursor(const uint8_t* q) {
  auto* pairs = game::Read<const uint32_t*>(q, game::Q::kPairs);
  if (!pairs) return -1;
  return static_cast<int>(pairs[game::Read<int>(q, game::Q::kIndex) * 2 + 1]);
}

void SetPhase(void* s, Entry& e, Phase phase, const char* why) {
  Log("Zombie: S=%p %s", s, why);
  e.phase = phase;
  e.phaseBlend = -1;
  e.updates = 0;
  e.applied = false;
  e.q1Cursor = -1;
  e.q1Stall = 0;
  e.q1Advanced = false;
  e.headingHeld = false;
  e.fallExitAt = -1;
}

// End of clip like on the PS1: 8007F564 sets E+320 bit 8 when track 800A2408[type] reaches the
// last keyframe; for the zombie (PS1 resource type 2) it is track 1. The PC instead counts the
// completions of track 0 (S+34), which in 08, 0E and 1C loops in only 2 units against the 15-20
// of the other tracks. On the PC the end of track 1 shows when its cursor goes back (loop) or
// stays still for 4 updates after advancing (clip held on its last pose).
bool Q1Completed(const uint8_t* o, Entry& e) {
  const uint8_t* q = ExitTrack(o);
  if (!q) return false;
  // In the update of a hit without reaction the PC moves the cursors back by 5 and HitAfter restores
  // them: here it would look like the end of the clip (hit during 0F, the zombie went back to the enemy logic
  // and walked while still sitting).
  if (HitRewindActive()) return false;
  const int cursor = game::Read<int>(q, game::Q::kCursor);
  bool done = false;
  if (e.q1Cursor >= 0) {
    if (cursor < e.q1Cursor && e.q1Advanced) {
      done = true;
    } else if (cursor > e.q1Cursor) {
      e.q1Advanced = true;
      e.q1Stall = 0;
    } else if (e.q1Advanced && ++e.q1Stall >= 4) {
      done = true;
    }
  }
  e.q1Cursor = cursor;
  return done;
}

// PS1 blend of the ported clips (blend_logic.h): E+8B0 passed by whoever changes clip in the zombie
// bank (ENEMY_SLOT40). -1 = PC transition unchanged.
int Ps1Blend(Phase phase) {
  switch (phase) {
    case Phase::Knockback: return 4;     // 801012CC: 8008035C(E,6,0x20/reaction,4)
    case Phase::BackFall: return 4;      // 80102D74: 80080334(E,8,4)
    case Phase::Supine: return ps1::kBlendStateChange;  // 80102D74: 800802C8(E,4,0E)
    case Phase::SitUp: return 2;         // 80104108: 80080334(E,0F,2)
    case Phase::Fall: return 4;          // 80102828: 8008035C(E,0xC,0x23,4)
    case Phase::GetUp2: return 2;        // 80080334(E,0x11,2)
    case Phase::Lunge: return 2;         // 80102828: 8008035C(E,0xB,0x1C,2)
    case Phase::LungeMiss: return 2;     // 80103868: 80080334(E,0x1F,2)
    case Phase::LungeGrab: return 2;     // 80103868: 80080334(E,0x1D,2)
    case Phase::LungeBite: return 2;     // 80103868: 80080334(E,0x1E,2)
    case Phase::Feed: return ps1::kBlendStateChange;  // 80102828: 800802C8(E,0,0x17)
    default: return -1;                  // GetUp1 (0x10): call not read yet
  }
}

const void* g_blendModel = nullptr;  // model of the next application to blend
void* g_blendSubject = nullptr;
int g_blendUnits = -1;

void RequestPs1Blend(void* s, int ps1Units) {
  if (!g_config.ps1ZombieBlend || ps1Units < 0) return;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return;
  g_blendModel = game::Read<void*>(o, game::O::kModel);
  g_blendSubject = s;
  g_blendUnits = ps1Units;
}

using ApplySequenceFn = int(__fastcall*)(void* m, void* edx, int track, int selector, int transition, float rate);
ApplySequenceFn g_applySequence = nullptr;

int __fastcall ApplySequenceDetour(void* m, void* edx, int track, int selector, int transition, float rate) {
  if (m && m == g_blendModel && g_blendUnits >= 0) {
    const int pc = ps1::PcTransition(g_blendUnits, rate);
    if (track == 0) {
      auto it = g_entries.find(g_blendSubject);
      if (it != g_entries.end()) it->second.stillUpdates = ps1::BlendStillFrames(g_blendUnits);
    }
    transition = pc;
  }
  return g_applySequence(m, edx, track, selector, transition, rate);
}

int ApplyPendingBlend(void* s, int result) {
  auto it = g_entries.find(s);
  if (it == g_entries.end() || it->second.nextBlend < 0 || it->second.phase != Phase::Idle) return result;
  const bool walk = (result >= 1 && result <= 5) || (result >= 0x26 && result <= 0x29);
  if (!walk || result == game::Read<int>(s, game::S::kSelector)) return result;
  RequestPs1Blend(s, it->second.nextBlend);
  it->second.nextBlend = -1;
  return result;
}

void HoldSequence(void* s, Entry& e, int& result) {
  result = SelectorOf(e);
  RequestPs1Blend(s, e.phaseBlend >= 0 ? e.phaseBlend : Ps1Blend(e.phase));
  *game::Field<uint8_t>(s, game::S::kNextThreshold) = e.phase == Phase::Fall ? 0 : 1;
  *game::Field<float>(s, game::S::kNextSpeed) = PhaseSpeed(e.phase);
}

void SetHeading(uint8_t* o, float fx, float fz) {
  float* m = game::Field<float>(o, game::O::kMatrix);
  // Rotation around Y: row 0 = (fz, 0, -fx), row 2 = (fx, 0, fz); row 1 unchanged.
  m[0] = fz;
  m[1] = 0.0f;
  m[2] = -fx;
  float* row2 = m + 2 * game::O::kMatrixRowFloats;
  row2[0] = fx;
  row2[1] = 0.0f;
  row2[2] = fz;
  if (WorldOwnsObject(o)) SyncObjectPose(o);
}

ps1::Vec2 PlayerForward(const uint8_t* t) {
  // The player's front is their local -Z (0043A8A0 transforms (0,0,-1)).
  const float* row2 = reinterpret_cast<const float*>(t + game::O::kForward);
  return {-row2[0], -row2[2]};
}

// Moves the zombie by (dx, dz) if it does not cross a ROOMENV wall.
// TODO: on the PS1 8007F404 -> 8007CB9C slides along the wall; here it stops.
void MoveZombie(uint8_t* o, float dx, float dz) {
  if (WorldMove(o, dx, dz)) return;
  float* zp = game::Field<float>(o, game::O::kPos);
  const auto& walls = CurrentRoomWalls();
  const ps1::Vec2 from{zp[0], -zp[2]}, to{zp[0] + dx, -(zp[2] + dz)};
  if (!walls.empty() && ps1::FirstBlockingWall(walls, from, to) >= 0) return;
  zp[0] += dx;
  zp[2] += dz;
}

float AngleOf(ps1::Vec2 v) { return std::atan2(v.x, v.z); }

int AngleUnits(float radians) {
  const float kPi = 3.14159265f;
  while (radians > kPi) radians -= 2.0f * kPi;
  while (radians < -kPi) radians += 2.0f * kPi;
  return static_cast<int>(std::lround(radians / ps1::kAngleUnit));
}

void HoldPlayer(uint8_t* t, float x, float z) {
  float* pp = game::Field<float>(t, game::O::kPos);
  pp[0] = x;
  pp[2] = z;
  if (WorldEnabled()) SyncObjectPose(t);
}

namespace {

bool nativeOnlyUpdate = false;

struct NativeOnly {
  bool previous = nativeOnlyUpdate;
  NativeOnly() { nativeOnlyUpdate = true; }
  ~NativeOnly() { nativeOnlyUpdate = previous; }
};

int __fastcall MapAnimDetour(void* s, void* edx, int request) {
  if (nativeOnlyUpdate) {
    const int native = g_mapAnim(s, edx, request);
    // Also in the updates without enemy logic (messages, picking up an item) the body stays in the PS1 death clip:
    // the PC mapper would choose its own (07, standing) and the corpse would get up for a moment (picking
    // up the key in 08/01).
    auto held = g_entries.find(s);
    if (g_config.ps1ZombieDeath && held != g_entries.end() && held->second.phase == Phase::Death) {
      return SelectorOf(held->second);
    }
    return native;
  }
  request = HitMapRequest(s, request);  // hit without PC reaction: the request from before
  g_blendModel = nullptr;  // the PS1 blend applies only to the application of this request
  return ApplyPendingBlend(s, ApplyBiteClip(s, request, ApplyWalkHold(s, request, MapAnimInner(s, edx, request))));
}

// The same entry check as 0043A8A0 (game.h, kUpdateGateCall): outside gameplay (cut-scenes, menus) or
// with the zombie not active the native update leaves at once with -1 and the enemy logic does not run.
bool g_updateGateReady = false;
bool NativeAiRuns(void* s) {
  if (!g_updateGateReady) return true;
  const auto* state = *reinterpret_cast<const uint8_t* const*>(game::kGameState);
  if (!state || !game::Read<uint8_t>(s, game::kEnemyActive)) return false;
  const uint8_t mode = state[game::kGameModeOffset];
  const int scene = *reinterpret_cast<const int*>(game::kSceneId);
  if (scene == 4) return mode == 3;
  if (scene == 0x5B) return mode == 3 || mode == 6;
  return mode == 3 || mode == 4;
}

// The PS1 turns the zombie only while walking (02 and 04: 0.17 and 0.24 degrees per frame on average); in
// 08, 0E, 0F, 0x10, 0x11, 0x15, 0x20, 0x1B, 0x23 and in the deaths the direction E+1C6 never changes (all
// DuckStation recordings, 2026-10-22). The PC, with its enemy logic that keeps turning it, did 0.9 degrees per update
// in 0E, 1.2 in 0F and 0.75 in 0x20 (while lying down it followed the player with its whole body).
// The lunge has its own rotation (80103868) and stays as it is.
// Direction of S before the PC enemy logic of this update: the new phase starts from there, without the 3 degrees
// the enemy logic already added in the update in which the phase began.
const void* g_preHeadingOf = nullptr;
float g_preFx = 0.0f, g_preFz = 1.0f;

void HoldHeading(void* s, Entry& e) {
  if (IsLungePhase(e.phase) && e.phase != Phase::Knockback) return;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return;
  const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
  if (!e.headingHeld) {
    e.headingHeld = true;
    const bool pre = g_preHeadingOf == s;
    e.heldFx = pre ? g_preFx : fw[0];
    e.heldFz = pre ? g_preFz : fw[2];
  }
  if (fw[0] != e.heldFx || fw[2] != e.heldFz) SetHeading(o, e.heldFx, e.heldFz);
}

int __fastcall UpdateEnemyDetour(void* s, void* edx) {
  const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  // The blood on hit (0043AC86) is created inside 0043A8A0: zombie_blood reads S from here.
  struct BloodScope {
    BloodScope(void* s) { SetBloodSubject(s); }
    ~BloodScope() { SetBloodSubject(nullptr); }
  } bloodScope(s);
  // Cut-scene or zombie not active: the native enemy logic is still, and so are pushes, bite, separation, phases
  // and world (restaurant 08/00: the zombies walked and bit during the window cut-scene).
  if (!NativeAiRuns(s)) {
    NativeOnly nativeOnly;
    return g_updateEnemy(s, edx);
  }
  // 0043D2F0/0043D340 call this function for suspend/resume without a world tick.
  // Their native side effects are kept, but navigation, phases, cooldowns and the
  // mapper decisions do not advance. Only the verified 43C850 gameplay call owns them.
  if (WorldEnabled() && caller != game::kWorldEnemyUpdateReturn) {
    NativeOnly nativeOnly;
    return g_updateEnemy(s, edx);
  }
  WorldBeginUpdate(s, caller);
  struct FinishWorld {
    void* s;
    ~FinishWorld() { WorldFinishUpdate(s); }
  } finishWorld{s};
  // PS1 frame order: first the player (8006CF04 -> 80078580 -> 800785C0: step, push 80081C80 against
  // the zombie where the previous frame left it, walls 8007CB9C), then the enemies (8006CF24 -> 800800A0
  // -> 8007FEBC: state, separation 80081618, movement). After SeparateZombie the overlap was gone and the
  // player walking in contact during the lunge was not pushed (PS1: +150, then +92 per frame).
  PushPlayer(s);
  const BiteGate bite = BiteBefore(s);
  // 0043B40D: if the moving player bumped into this enemy (S+10 == T+FC, T+105 == 0) the PC knocks it
  // down (RequestAnim(S, 0x0E, 1)). The PS1 zombie has no such fall: it enters state 6 only from a hit
  // (801012CC) or from the end of the bite; against the player there is only the push 80081C80 (the
  // player stays blocked, the zombie still). During the zombie's update the player's collision partner
  // is hidden.
  int* partner = nullptr;
  int savedPartner = 0;
  if (g_config.ps1ZombiePush && ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) {
    uint8_t *po, *pt;
    if (GetObjects(s, po, pt)) {
      int* p = game::Field<int>(pt, 0xFC);
      if (*p == game::Read<int>(s, game::S::kIndex)) {
        partner = p;
        savedPartner = *p;
        *p = -1;
      }
    }
  }
  g_preHeadingOf = nullptr;
  if (ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) {
    uint8_t *ho, *ht;
    if (GetObjects(s, ho, ht)) {
      const float* fw = reinterpret_cast<const float*>(ho + game::O::kForward);
      g_preHeadingOf = s;
      g_preFx = fw[0];
      g_preFz = fw[2];
    }
  }
  const HitGate hit = HitBefore(s);
  WalkAudioBefore(s);
  const int result = UpdateWithNavigation(s, edx);
  HitAfter(s, hit);
  if (partner) *partner = savedPartner;
  BiteAfter(s, bite);
  UpdateBiteSequence(s);
  SeparateZombie(s);
  ScaleWalk(s);
  if (ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) {
    static const Entry kNoEntry;
    auto pose = g_entries.find(s);
    UpdatePose(s, pose != g_entries.end() ? pose->second : kNoEntry);
  }
  WalkAudioAfter(s);
  if (!g_config.ps1ZombieStumble && !g_config.ps1ZombieBackFall && !g_config.ps1ZombieNavigation &&
      !g_config.ps1ZombieBite && !g_config.ps1ZombieDeath) {
    g_entries.clear();
    return result;
  }
  if (!ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) return result;
  auto it = g_entries.find(s);
  if (it == g_entries.end()) return result;
  Entry& e = it->second;
  if (e.phase == Phase::Idle) return result;
  HoldHeading(s, e);
  if (e.phase == Phase::Death) {  // no time limit: the body stays on its last pose
    UpdateDeath(s, e);
    return result;
  }
  if (e.phase == Phase::Feed) {  // E+316 = 4: no enemy logic, the zombie stays on the body
    uint8_t *o, *t;
    if (GetObjects(s, o, t)) {
      float* accum = game::Field<float>(o, game::O::kAccum);
      accum[0] = accum[1] = accum[2] = 0.0f;
    }
    return result;
  }
  if (++e.updates > kPhaseTimeout) {
    SetPhase(s, e, Phase::Idle, "timeout, back to the enemy logic");
    return result;
  }
  if (IsLungePhase(e.phase)) {
    UpdateLunge(s, e);
    return result;
  }
  UpdateFallPhase(s, e);
  return result;
}

}  // namespace

}  // namespace sf::features::zombie

namespace sf::features {

using namespace zombie;

bool InstallZombies() {
  const game::ByteSignature checks[] = {
      {game::kAccumulate, game::kAccumulatePrologue, sizeof(game::kAccumulatePrologue)},
      {game::kHitPlayer, game::kHitPlayerPrologue, sizeof(game::kHitPlayerPrologue)},
      {game::kTurnToward, game::kTurnTowardPrologue, sizeof(game::kTurnTowardPrologue)},
      {game::kMapAnim, game::kMapAnimPrologue, sizeof(game::kMapAnimPrologue)},
      {game::kUpdateEnemy, game::kUpdateEnemyPrologue, sizeof(game::kUpdateEnemyPrologue)},
  };
  for (const auto& check : checks) {
    if (!game::CheckBytes(check)) {
      Log("Zombies: unexpected bytes at %08X, different executable? Feature off.",
          static_cast<unsigned>(check.address));
      return false;
    }
  }
  if (!game::CheckBytes({game::kWorldEnemyUpdateCall, game::kWorldEnemyUpdateCallBytes,
                         sizeof(game::kWorldEnemyUpdateCallBytes)})) {
    Log("ZombieWorld: unexpected gameplay call; PS1 world disabled");
    WorldBridgeReady(false);
    g_config.ps1ZombieWorld = false;
  }
  g_rng = GetTickCount();
  g_updateGateReady =
      game::CheckBytes({game::kSceneIdGetter, game::kSceneIdGetterBytes, sizeof(game::kSceneIdGetterBytes)}) &&
      game::CheckBytes({game::kUpdateGateCall, game::kUpdateGateCallBytes, sizeof(game::kUpdateGateCallBytes)});
  if (!g_updateGateReady) Log("Zombies: entry of 0043A8A0 not recognised, the port also runs outside gameplay");
  // Native lifetime notifications: the RequestAnim ABI and the initializer CALL are both byte-verified.
  if (!InstallZombieLife()) {
    Log("Zombies: zombie initialisation hook unavailable, zombie port not installed");
    return false;
  }
  // The update first: on its own it changes nothing (no phase active).
  const bool ready = Hook("UpdateEnemy", game::kUpdateEnemy, reinterpret_cast<void*>(&UpdateEnemyDetour),
                          reinterpret_cast<void**>(&g_updateEnemy)) &&
                     Hook("MapAnim", game::kMapAnim, reinterpret_cast<void*>(&MapAnimDetour),
                          reinterpret_cast<void**>(&g_mapAnim));
  if (!ready) WorldBridgeReady(false);
  if (ready) InstallZombieHeightSupport();
  if (ready) InstallZombiePose();
  if (ready) InstallZombieBlood();
  if (ready) InstallZombieDeath();
  if (ready) InstallZombieWalkAudio();
  if (ready && g_config.ps1ZombieBlend) {
    if (!game::CheckBytes({game::kApplySequence, game::kApplySequencePrologue,
                           sizeof(game::kApplySequencePrologue)}) ||
        !game::CheckBytes({game::kApplySequenceCall, game::kApplySequenceCallBytes,
                           sizeof(game::kApplySequenceCallBytes)})) {
      Log("Ps1ZombieBlend: unexpected bytes at %08X, PC blend unchanged", static_cast<unsigned>(game::kApplySequence));
    } else {
      Hook("ApplySequence", game::kApplySequence, reinterpret_cast<void*>(&ApplySequenceDetour),
           reinterpret_cast<void**>(&g_applySequence));
    }
  }
  return ready;
}

}  // namespace sf::features
