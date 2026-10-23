// PS1 zombie bite, grab from behind and lunge.
#include "zombie_shared.h"

namespace sf::features::zombie {

// Ps1ZombieBite -------------------------------------------------------------------
// The PC attacks (request 8) as soon as the player is in range; the PS1 waits 16 frames in a row
// in the cone in front of the player and lets only one zombie bite at a time. To block the attack
// the update runs with O+38 = 0 (ranges 8/9 become 0) and then the value is restored. During an
// attack already started (S+1C == 8) nothing is touched.
constexpr DWORD kBiterTimeoutMs = 500;

struct BiteEntry {
  ps1::BiteCounter counter;
  DWORD lastSeen = 0;
  DWORD clipStart = 0;    // GetTickCount when 0x1B was chosen
  bool clipDone = false;  // the 0x1B of this attack has ended: the attack closes
};
std::unordered_map<void*, BiteEntry> g_bites;
void* g_biter = nullptr;  // zombie that is biting (PS1 bit 0x20000)
DWORD g_biterSeen = 0;
int g_backGrabCooldown = 0;  // 80105FC0: non-zero = wait in progress
// Grab from behind in progress (state 0x0E, 80103FA4): the zombie is the "biter" (it blocks the
// others), stays still and at each of its updates the player turns towards it at the PS1 step. On the
// PS1 the player's controls are suspended (80062DEC = 0) until the view has finished: here the player
// stays where the grab started.
// TODO: zombie animation 4 (80080334(E,4,2)) and view pitch.
constexpr int kGrabMaxUpdates = 45;
struct Grab {
  void* s = nullptr;
  int updates = 0;
  float px = 0.0f, pz = 0.0f;  // player position at the start of the grab
};
Grab g_grab;
bool GrabActiveFor(void* s) { return g_grab.s == s; }

// View rotation at the PS1 rate. The PC turns the view with 0045FE30 (20% of the rest per tick, i.e.
// almost all of it in ~0.3 s: the "snap"); the PS1 with 80078A58/80079064 at a fixed step with a tail.
// Here the PC computation of the direction (0045FE30) is used and the PS1 step is imposed at every
// update by writing O+A4/O+A8 = step / 0.2, so the physics applies exactly that step.
// ASSUMPTION: one physics tick per enemy logic update.
struct ViewTurnRun {
  bool active = false;
  ps1::ViewTurn turn;
  int frames = 0;
  const uint8_t* target = nullptr;  // zombie to turn towards
  float tx = 0.0f, tz = 0.0f;       // look-at point (the head, see ZombieHeadPoint)
  float lastError = 0.0f;           // geometric |angle| at the previous update (degrees)
  bool checked = false;             // direction checked after the first step
};
ViewTurnRun g_view;
// Direction of O+A4 relative to the geometric angle (atan2(x, z) from PlayerForward to the zombie):
// +1/-1 once observed, 0 while unknown.
int g_turnSignMap = 0;

float AngleToPointDegrees(const uint8_t* t, float x, float z);
bool ZombieHeadPoint(const uint8_t* o, float head[3]);

void CancelPcTurn(uint8_t* t) {
  *game::Field<uint8_t>(t, game::O::kTurnActive) = 0;
  *game::Field<float>(t, game::O::kTurnYaw) = 0.0f;
  *game::Field<float>(t, game::O::kTurnPitch) = 0.0f;
}

void StartViewTurnTo(uint8_t* t, const uint8_t* o, int yawStep, int pitchStep) {
  // 80078D38(E,0,dist,2,mode): the look-at point is not the zombie's origin but the node of the part
  // of type 2 (records of 0x5C from E+392 with +0x39A == 2, node [+0x3BC]+0x38), as it is in that frame
  // of the animation. On the real PS1, at frame 12 of the bite the 0x1B head is 16-36 units to the side
  // and the turn is 48-57 units in 7 frames; straight towards the origin the PC did not turn at all
  // (rest < 12 dropped) and the bite lasted 13 frames instead of 23. If the bones cannot be read, the
  // origin as before.
  float target[3];
  const bool head = ZombieHeadPoint(o, target);
  if (!head) memcpy(target, o + game::O::kPos, sizeof(target));
  // Angle from the geometry: the player's direction (the same as the cone, 800824B0) and the look-at
  // point. 0045FE30 is wrong in some cases (168 and 155 degrees with the zombie in front, when the
  // height deviates from the reference [005BDB14] and goes through 00460140); here it only tells the
  // direction of O+A4.
  const float geomDeg = AngleToPointDegrees(t, target[0], target[2]);
  CancelPcTurn(t);
  const float* zp = reinterpret_cast<const float*>(o + game::O::kPos);
  g_turnToward(t, nullptr, target[0], zp[1] - game::kAttackHeadOffset, target[2]);
  float pcDeg = game::Read<float>(t, game::O::kTurnYaw);
  if (!std::isfinite(pcDeg)) pcDeg = 0.0f;
  CancelPcTurn(t);
  if (std::fabs(geomDeg) > 5.0f && std::fabs(std::fabs(pcDeg) - std::fabs(geomDeg)) < 20.0f) {
    g_turnSignMap = (pcDeg >= 0.0f) == (geomDeg >= 0.0f) ? 1 : -1;
  }
  const int map = g_turnSignMap ? g_turnSignMap : ((pcDeg >= 0.0f) == (geomDeg >= 0.0f) ? 1 : -1);
  const float yawDeg = geomDeg * map;
  // Pitch: on the PS1 towards the zombie's height (80078A58), ~0 at these distances; the PC value for a
  // target behind saturates at 30 degrees (view down). TODO: real PS1 pitch.
  const float pitchDeg = 0.0f;
  const float unitsPerDeg = 4096.0f / 360.0f;
  g_view = ViewTurnRun();
  g_view.turn = ps1::StartViewTurn(static_cast<int>(std::lround(yawDeg * unitsPerDeg)),
                                   static_cast<int>(std::lround(pitchDeg * unitsPerDeg)), yawStep, pitchStep);
  g_view.active = true;
  g_view.target = o;
  g_view.tx = target[0];
  g_view.tz = target[2];
  g_view.lastError = std::fabs(geomDeg);
}

// One step per update; returns true once the rotation is over.
bool StepViewTurnTo(uint8_t* t) {
  if (!g_view.active) return true;
  // After the first step: if the geometric angle grew, the direction was wrong.
  if (g_view.frames == 1 && !g_view.checked && g_view.target) {
    g_view.checked = true;
    const float err = std::fabs(AngleToPointDegrees(t, g_view.tx, g_view.tz));
    if (err > g_view.lastError + 1.0f) {
      g_view.turn.yaw.sign = -g_view.turn.yaw.sign;
      g_turnSignMap = -(g_turnSignMap ? g_turnSignMap : 1);
    }
  }
  int yaw = 0, pitch = 0;
  ps1::StepViewTurn(g_view.turn, yaw, pitch);
  const float degPerUnit = 360.0f / 4096.0f;
  if (!g_view.turn.yaw.active && yaw == 0 && pitch == 0) {
    CancelPcTurn(t);
    g_view.active = false;
    return true;
  }
  ++g_view.frames;
  *game::Field<float>(t, game::O::kTurnYaw) = yaw * degPerUnit / game::kTurnApplyFraction;
  *game::Field<float>(t, game::O::kTurnPitch) = pitch * degPerUnit / game::kTurnApplyFraction;
  *game::Field<uint8_t>(t, game::O::kTurnActive) = 1;
  return false;
}

// Bitten player sequence (800830F8): one zombie at a time (800BB198). The player stays still (controls
// blocked, 80062DEC = 0) for the whole sequence. The jolt of the first 12 frames (800A865E and 800B3EF8)
// goes to the player's view with Ps1DamageView (ps1/damage_view_logic.h).
struct BiteSequence {
  void* s = nullptr;
  int frame = 0;
  bool turning = false;
  float px = 0.0f, pz = 0.0f;
  ps1::BiteJolt jolt;
};

BiteSequence g_seq;
void ResetBiteSequence() {
  g_seq = BiteSequence();
  SetDamageViewJolt(0, ps1::kViewLookY);
}
constexpr int kOrphanUpdates = 30;  // updates of other zombies without the biter's one
int g_ownerMissed = 0;

bool IsBiting(void* s) {
  return g_seq.s == s || g_grab.s == s || game::Read<int>(s, game::S::kRequest) == kAttackRequest;
}

void StartBiteSequence(void* s, const uint8_t* t) {
  const float* pp = reinterpret_cast<const float*>(t + game::O::kPos);
  g_seq = BiteSequence();
  g_seq.s = s;
  g_seq.px = pp[0];
  g_seq.pz = pp[2];
}

// Player grabbed or bitten: controls blocked (80062DEC = 0 on the PS1).
bool PlayerHeld() { return g_grab.s || g_seq.s || g_biter; }

float AngleToPointDegrees(const uint8_t* t, float x, float z) {
  const float* pp = reinterpret_cast<const float*>(t + game::O::kPos);
  const ps1::Vec2 f = PlayerForward(t);
  const float kPi = 3.14159265f;
  float d = std::atan2(x - pp[0], z - pp[2]) - std::atan2(f.x, f.z);
  while (d > kPi) d -= 2.0f * kPi;
  while (d < -kPi) d += 2.0f * kPi;
  return d * 180.0f / kPi;
}

// The PC model has sequence `clip` (track 0, Q+0C).
bool ModelHasClip(const uint8_t* o, int clip) {
  auto* m = game::Read<uint8_t*>(o, game::O::kModel);
  if (!m) return false;
  auto* tracks = game::Read<uint8_t*>(m, game::M::kTracks);
  if (!tracks || game::Read<unsigned>(m, game::M::kTrackCount) == 0) return false;
  return clip >= 0 && clip < game::Read<int>(tracks, game::Q::kCount);
}

BiteGate BiteBefore(void* s) {
  BiteGate g;
  if (!g_config.ps1ZombieBite || !ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) return g;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return g;
  const DWORD now = GetTickCount();
  BiteEntry& b = g_bites[s];
  // NativeZombieLife/ResetZombieRoom own the resets; a long pause is not a new life.
  b.lastSeen = now;
  if (g_biter && now - g_biterSeen > kBiterTimeoutMs) {
    if (g_grab.s == g_biter) g_grab = Grab();
    g_biter = nullptr;
  }
  // 80105180 -> 80105160: the wait 80105FC0 drops by 1 at every update of every zombie.
  if (g_backGrabCooldown > 0) --g_backGrabCooldown;
  // On the PS1 the biting zombie is a pointer in the player's entity (800BB198) and the sequence runs
  // in its update: it disappears with it. Here the state is global and stayed open if the zombie
  // disappeared halfway through the bite (back in 07/00, the new zombie stayed in the cone at <= 1000
  // for 67 frames without biting). It is released when other zombies update kOrphanUpdates times
  // without its update.
  if (void* owner = g_seq.s ? g_seq.s : g_grab.s) {
    if (owner == s) {
      g_ownerMissed = 0;
    } else if (++g_ownerMissed > kOrphanUpdates) {
      ResetBiteSequence();
      g_grab = Grab();
      if (g_biter == owner) g_biter = nullptr;
      g_ownerMissed = 0;
    }
  }

  // Grab from behind in progress for this zombie: turns the player, no attack.
  if (g_grab.s == s) {
    g_biter = s;
    g_biterSeen = now;
    const float* gp = reinterpret_cast<const float*>(o + game::O::kPos);
    float* pp = game::Field<float>(t, game::O::kPos);
    if (g_grab.updates == 0) {
      g_grab.px = pp[0];
      g_grab.pz = pp[2];
    }
    pp[0] = g_grab.px;  // controls suspended: the player does not move
    pp[2] = g_grab.pz;
    const ps1::Vec2 toZombie{gp[0] - pp[0], gp[2] - pp[2]};
    // 80078D38(E,0,dist,2,1): view towards the zombie, mode 1 (0x61 per frame, pitch 0x2B), applied by
    // the PC view rotation at the PS1 step.
    if (g_grab.updates == 0) StartViewTurnTo(t, o, 0x61, 0x2B);
    const bool done = StepViewTurnTo(t);
    ++g_grab.updates;
    // The zombie looks at the player and does not move (accumulator cleared after the update).
    const float len = std::sqrt(toZombie.x * toZombie.x + toZombie.z * toZombie.z);
    if (len > 0.0f) SetHeading(o, -toZombie.x / len, -toZombie.z / len);
    g.freeze = true;
    if (done || g_grab.updates >= kGrabMaxUpdates) {
      g_grab = Grab();
      b.counter = ps1::BiteCounter();                    // 80103FA4 -> state 4: the counter restarts
      g_entries[s].forceWalk = 2 + (NextRngByte() & 1);  // 80101CE4(E, rand & 1 | 2)
    }
    g.o = o;
    g.savedScale = game::Read<float>(o, game::O::kAttackScale);
    g.blocked = true;
    *game::Field<float>(o, game::O::kAttackScale) = 0.0f;
    return g;
  }

  // Lunge, 0x20 reaction and bite sequence: no PC attack (PS1 state 0xB/6).
  auto phaseIt = g_entries.find(s);
  if (phaseIt != g_entries.end() && phaseIt->second.phase != Phase::Idle) {
    g.o = o;
    g.savedScale = game::Read<float>(o, game::O::kAttackScale);
    g.blocked = true;
    *game::Field<float>(o, game::O::kAttackScale) = 0.0f;
    return g;
  }

  g.wasAttacking = game::Read<int>(s, game::S::kRequest) == kAttackRequest;
  if (g.wasAttacking && !b.clipDone) return g;
  if (g.wasAttacking) {
    // 80102C38 -> state 4: once 0x1B is over the PS1 goes back to the chase and the counter restarts.
    // With the ranges at 0 the PC enemy logic leaves request 8.
    g.o = o;
    g.savedScale = game::Read<float>(o, game::O::kAttackScale);
    g.blocked = true;
    *game::Field<float>(o, game::O::kAttackScale) = 0.0f;
    return g;
  }
  b.clipDone = false;
  // Cone, bite counter and grab from behind exist only in PS1 state 4 (801027C0 -> 80102828, 80101948).
  // ASSUMPTION: on the PC state 4 is walk request 02..04; with 01 the zombie sleeps (the "body" in the
  // 07/00 alley grabbed and pushed the player before getting up).
  if (!IsLocomotion(game::Read<int>(s, game::S::kRequest))) return g;

  const float* op = reinterpret_cast<const float*>(o + game::O::kPos);
  const float* tp = reinterpret_cast<const float*>(t + game::O::kPos);
  const float distance = Measure(o, t).distance;
  const bool inCone = ps1::InBiteCone({tp[0], tp[2]}, PlayerForward(t), {op[0], op[2]});
  if (PlayerDying()) {
    // 80102828 zone 4 (8007F4E8 < 1000): after 16 frames in the cone (E+360, the same counter as the bite)
    // 80100628 refuses the bite (800BB19C < 2) and with the player in state 3 E+316 = 4, 800802C8(E, 0, 0x17):
    // the zombie bends over the body. Before that it stays still in front of the player.
    if (ps1::UpdateBiteCounter(b.counter, distance, inCone) && ModelHasClip(o, kFeedClip)) {
      Entry& e = g_entries[s];
      SetPhase(s, e, Phase::Feed, "player dead: bends over the body (0x17, 80102828)");
      e.phaseBlend = ps1::kBlendStateChange;
    }
    g.freeze = distance < 1000.0f;
    g.o = o;
    g.savedScale = game::Read<float>(o, game::O::kAttackScale);
    g.blocked = true;
    *game::Field<float>(o, game::O::kAttackScale) = 0.0f;
    return g;
  }
  const bool ready = ps1::UpdateBiteCounter(b.counter, distance, inCone);
  const bool other = (g_biter && g_biter != s) || g_seq.s;
  if (ps1::BackGrabInRange(distance, inCone) && !other && g_backGrabCooldown == 0 && !g_grab.s) {
    Log("Zombie: S=%p grab from behind (distance %.0f): the player turns", s, distance);
    g_backGrabCooldown = ps1::kBackGrabCooldown;
    g_grab = Grab();
    g_grab.s = s;
    g_biter = s;
    g_biterSeen = now;
  }
  const float scale = game::Read<float>(o, game::O::kAttackScale);
  if (ready && !other) {
    g.ready = true;
    return g;
  }
  g.o = o;
  g.savedScale = scale;
  g.blocked = true;
  *game::Field<float>(o, game::O::kAttackScale) = 0.0f;
  return g;
}

void BiteAfter(void* s, const BiteGate& g) {
  if (g.blocked) *game::Field<float>(g.o, game::O::kAttackScale) = g.savedScale;
  if (g.freeze) {
    float* accum = game::Field<float>(g.o, game::O::kAccum);
    accum[0] = accum[1] = accum[2] = 0.0f;
  }
  if (!g_config.ps1ZombieBite || !ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) return;
  auto it = g_bites.find(s);
  // 80102828: with the counter ready the PS1 goes straight to state 5 (800802C8(E,5,E+85E)), with no
  // other condition on the zombie's orientation. The PC instead asks for the attack (request 8) only if
  // the zombie is aligned and in range (0043A8A0): zombie in the cone at 877-906 for 45 updates without
  // biting. Here it is requested by us; and while 0x1B runs it is requested again, because the PC enemy logic
  // would go back to the walk.
  {
    const int request = game::Read<int>(s, game::S::kRequest);
    const bool biting = it != g_bites.end() && it->second.clipStart != 0 && !it->second.clipDone &&
                        (game::Read<int>(s, game::S::kSelector) == kBiteClip ||
                         game::Read<int>(s, game::S::kSelector) == kBiteClipAfter2F);
    if (request != kAttackRequest && (g.ready || biting)) g_requestAnimCall(s, nullptr, kAttackRequest, 1);
  }
  if (PlayerDying() && !g.wasAttacking && game::Read<int>(s, game::S::kRequest) == kAttackRequest && g_seq.s != s) {
    // 80100628 refuses the bite with the player in state 3: the attack the PC has just asked for goes back
    // to the walk (request 02).
    g_requestAnimCall(s, nullptr, 0x02, 1);
  }
  const bool attacking = game::Read<int>(s, game::S::kRequest) == kAttackRequest;
  if (attacking) {
    g_biter = s;
    g_biterSeen = GetTickCount();
    uint8_t *o, *t;
    const bool objects = GetObjects(s, o, t);
    if (!g.wasAttacking && it != g_bites.end()) {
      it->second.counter = ps1::BiteCounter();  // 80102828: back in state 4
      // 80102C38, substate 0: 80081FF0(player, zombie) and the push goes into the zombie's velocity
      // (E+87C/E+884): the zombie leaves the player's 600 + 380 circle.
      // ASSUMPTION: 800BB044/800BB048 hold 600/380 (last values of 80081C80).
      if (objects) {
        float* zp = game::Field<float>(o, game::O::kPos);
        const float* pp = reinterpret_cast<const float*>(t + game::O::kPos);
        const float dy = zp[1] - (pp[1] + kPlayerEyeHeight);
        const ps1::Vec2 push = ps1::PlayerPush({zp[0], zp[2]}, {pp[0], pp[2]}, dy, ps1::kZombieRadius);
        const ps1::Vec2 from{zp[0], -zp[2]}, to{zp[0] + push.x, -(zp[2] + push.z)};
        const auto& walls = CurrentRoomWalls();
        if ((push.x != 0.0f || push.z != 0.0f) && (walls.empty() || ps1::FirstBlockingWall(walls, from, to) < 0)) {
          zp[0] += push.x;
          zp[2] += push.z;
        }
      }
      // 80102828 -> 80084D84(E,6,0): on the PS1 the damage starts when the bite is decided and cannot miss.
      // On the PC it is given here with the game's function and the delayed hit (S+50) is disarmed, which
      // would otherwise arrive at the end of the clip and could miss.
      const int sel = game::Read<int>(s, game::S::kSelector);
      if (sel == kBiteClip || sel == kBiteClipAfter2F) {
        *game::Field<int>(s, game::S::kHitArmed) = 0;
        g_hitPlayer(s, nullptr);
        if (objects) CancelPcTurn(t);  // the sequence turns the view, at the PS1 step
        if (objects && !g_seq.s) StartBiteSequence(s, t);
      }
    }
    // State 5: the zombie does not walk during the bite.
    if (objects) {
      float* accum = game::Field<float>(o, game::O::kAccum);
      accum[0] = accum[1] = accum[2] = 0.0f;
    }
  } else if (g_biter == s && g_grab.s != s) {
    g_biter = nullptr;
  }
}

// Ps1ZombieBite: the PS1 bite (state 5, 80102C38) uses only 0x1B, or 0x24 if the previous animation was
// 0x2F (80080268(E,0x24)). At every new attack clip the PC mapper (00436EE6) picks one at random among
// 1B..21 and arms the hit (S+50 = 1): here only the chosen clip and its speed are replaced, the hit stays
// the PC one.
int ApplyBiteClip(void* s, int request, int result) {
  if (!g_config.ps1ZombieBite || request != kAttackRequestMap || result < 0x1B || result > 0x21 ||
      !ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) {
    return result;
  }
  const int current = game::Read<int>(s, game::S::kSelector);
  // Continuation of the running clip (00436EFE: selector 1B..21 and S+2D == 0).
  if (result == current && !game::Read<uint8_t>(s, game::S::kDone)) return result;
  BiteEntry& b = g_bites[s];
  if ((current == kBiteClip || current == kBiteClipAfter2F) && b.clipStart) {
    // End of 0x1B: the PS1 does not do another one (back to state 4). The mapper has just rearmed the
    // PC hit: it is disarmed and the finished clip is kept.
    b.clipStart = 0;
    b.clipDone = true;
    *game::Field<int>(s, game::S::kHitArmed) = 0;
    return current;
  }
  const int clip = current == 0x2F ? kBiteClipAfter2F : kBiteClip;
  *game::Field<float>(s, game::S::kNextSpeed) = g_config.biteSpeed;
  b.clipStart = GetTickCount();
  RequestPs1Blend(s, 2);  // 80102C38: 80080334(E,0x1B,2)
  return clip;
}

// 80102828: lunge roll in bands 2 and 3 (once per band entry).
bool TryStartLunge(void* s, Entry& e, int request, int& result) {
  (void)request;
  // The lunge starts only in the chase (state 4): never while this zombie bites.
  if (IsBiting(s)) return false;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return false;
  if (!IsLocomotion(e.prevRequest)) {  // entering state 4: E+358 = E+35C = 0
    ps1::ResetOnChaseStart(e.lungeBand);
    e.lungeBits = ps1::LungeBits();
  }
  const Geometry g = Measure(o, t);
  ps1::UpdateBand(e.lungeBand, g.distance);
  const int band = e.lungeBand.band;
  ps1::ApplyBandMask(e.lungeBits, band);
  const bool needRoll = (band == 2 && !(e.lungeBits.bits & ps1::kLungeBand2Bit)) ||
                        (band == 3 && !(e.lungeBits.bits & ps1::kLungeBand3Bit));
  if (!needRoll) return false;
  const bool inSector = InSectorOf(o, g.toX, g.toZ);  // 80100924(E,0x28)
  if (!ps1::ShouldStartLunge(e.lungeBits, band, NextRngByte(), inSector)) return false;
  SetPhase(s, e, Phase::Lunge, "lunge 1C");
  HoldSequence(s, e, result);
  return true;
}

// 800830F8 frame by frame for the zombie of the sequence.
void UpdateBiteSequence(void* s) {
  if (g_seq.s != s) return;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t) || g_seq.frame > 300) {
    ResetBiteSequence();
    return;
  }
  HoldPlayer(t, g_seq.px, g_seq.pz);
  // 8008445C: if the bite kills (80085144) the player goes straight to state 3 (8008369C) and state 2
  // (800830F8: jolt, rotation towards the zombie) does not start.
  if (PlayerDying()) {
    // 80102828: if the decided bite kills (8008445C sets 800BB19C = 3), in the same frame E+316 = 4 and
    // 800802C8(E, 0, 0x17) override state 5: no 0x1B bite, the zombie throws itself on the body (blend 0x180)
    // while the view falls (8008369C step 1). The player's state 2 (800830F8: jolt, rotation towards the
    // zombie, 0x20 push back) does not start.
    SetDamageViewJolt(0, ps1::kViewLookY);
    ResetBiteSequence();
    if (g_biter == s) g_biter = nullptr;
    g_bites[s].clipDone = true;
    Entry& dead = g_entries[s];
    dead.backRolled = false;
    dead.reactClip = 0;
    dead.pushSign = 1.0f;
    dead.slideP10 = 500;
    if (ModelHasClip(o, kFeedClip)) {
      SetPhase(s, dead, Phase::Feed, "deadly bite: throws itself on the body (0x17, 80102828)");
      dead.phaseBlend = ps1::kBlendStateChange;
    } else {
      dead.forceWalk = 2 + (NextRngByte() & 1);
      SetPhase(s, dead, Phase::Idle, "deadly bite, clip 0x17 missing: walks again");
    }
    return;
  }
  if (g_config.ps1DamageView && !g_seq.jolt.done) {
    ps1::StepBiteJolt(g_seq.jolt);  // 800830F8 steps 0-2 and 5, one frame per update
    SetDamageViewJolt(g_seq.jolt.yawOff, g_seq.jolt.lookY);
  }
  const int frame = g_seq.frame++;
  if (frame < ps1::kBiteShakeFrames) return;
  if (!g_seq.turning) {
    // Step 5: 80078D38(E,0,dist,2,0), mode 0 (0x7A per frame, pitch 0x36). Parameter 9 of the zombie
    // (0xA000) has no 0x1000, so the view turns towards it.
    StartViewTurnTo(t, o, ps1::kBiteTurnStep, 0x36);
    g_seq.turning = true;
  }
  if (!StepViewTurnTo(t)) return;
  // Steps 6-8: end of the rotation, the zombie is pushed back (parameter 9 & 0x8000) and the player is
  // free again (800BB19C = 1).
  ResetBiteSequence();
  if (g_biter == s) g_biter = nullptr;
  g_bites[s].clipDone = true;
  Entry& e = g_entries[s];
  e.backRolled = false;
  e.reactClip = 0;
  e.pushSign = 1.0f;  // 800830F8 step 7: E+32B = 0, front
  e.slideP10 = 500;
  SetPhase(s, e, Phase::Knockback, "pushed back after the bite: 0x20");
  ps1::StartKnockbackPush(e.push);  // 80102D74 case 0x1A: P+10 = 500, P+14 = 0x14
}

// End of the state 6 clip (80102D74 substate 2 -> 8010122C).
void FinishKnockback(void* s, Entry& e) {
  if (e.reactClip == 0x15) {
    // 8010122C: after 0x15 (also 0x0D, 0x13) 800802C8(E,4,0x10), on the ground with blend 0x180; then
    // 80104108 case 6: 80080334(E,0x11,2) and the walk (DuckStation 2026-10-21: 0x15 24 frames, 0x10 24 + 2
    // per command, 0x11 2 per command, y always 0).
    e.reactClip = 0;
    e.pushSign = 1.0f;
    e.slideP10 = 500;
    SetPhase(s, e, Phase::GetUp1, "after reaction 0x15: on the ground 10");
    e.phaseBlend = ps1::kBlendStateChange;
    return;
  }
  e.forceWalk = 2 + (NextRngByte() & 1);  // 800802C8(E,4,80101CE4(...))
  SetPhase(s, e, Phase::Idle, e.reactClip ? "end of the hit reaction, back to the enemy logic" : "0x20 without a fall, back to the enemy logic");
  e.reactClip = 0;
  e.pushSign = 1.0f;
  e.slideP10 = 500;
}

// The exit track went back to the start in this update (same criterion as Q1Completed, without
// touching its state).
bool ExitTrackWrapped(void* s, const Entry& e) {
  uint8_t *o, *t;
  if (!e.applied || !e.q1Advanced || e.q1Cursor < 0 || HitRewindActive() || !GetObjects(s, o, t)) return false;
  const uint8_t* q = ExitTrack(o);
  return q && game::Read<int>(q, game::Q::kCursor) < e.q1Cursor;
}

struct Mat4 {
  float m[16];
};

Mat4 MulMat(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      float v = 0.0f;
      for (int k = 0; k < 4; ++k) v += a.m[i * 4 + k] * b.m[k * 4 + j];
      r.m[i * 4 + j] = v;
    }
  }
  return r;
}

bool SafeCopy(const void* from, void* to, size_t size) {
  SIZE_T got = 0;
  return ReadProcessMemory(GetCurrentProcess(), from, to, size, &got) && got == size;
}

// Origin of bone `index` in the world: chain of the parents (animation x offset), then O's matrix
// (row vectors). Reads with ReadProcessMemory: the model may be missing.
bool BoneWorld(const uint8_t* o, unsigned index, float w[3]) {
  const uint8_t* m = nullptr;
  unsigned count = 0;
  const uint8_t* bones = nullptr;
  Mat4 object;
  if (!SafeCopy(o + game::O::kModel, &m, sizeof(m)) || !m || !SafeCopy(m + game::M::kBoneCount, &count, sizeof(count)) ||
      !SafeCopy(m + game::M::kBones, &bones, sizeof(bones)) || !bones || index >= count || count > 64 ||
      !SafeCopy(o + game::O::kMatrix, object.m, sizeof(object.m))) {
    return false;
  }
  Mat4 acc = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  const uint8_t* b = bones + index * game::M::kBoneSize;
  for (int depth = 0; b && depth < 64; ++depth) {
    uint8_t bone[game::M::kBoneSize];
    if (!SafeCopy(b, bone, sizeof(bone))) return false;
    Mat4 anim, offset;
    memcpy(anim.m, bone + game::M::kBoneAnim, sizeof(anim.m));
    memcpy(offset.m, bone + game::M::kBoneOffset, sizeof(offset.m));
    acc = MulMat(MulMat(acc, anim), offset);
    memcpy(&b, bone + game::M::kBoneParent, sizeof(b));
  }
  const float mx = acc.m[12], my = acc.m[13], mz = acc.m[14];
  w[0] = mx * object.m[0] + my * object.m[4] + mz * object.m[8] + object.m[12];
  w[1] = mx * object.m[1] + my * object.m[5] + mz * object.m[9] + object.m[13];
  w[2] = mx * object.m[2] + my * object.m[6] + mz * object.m[10] + object.m[14];
  return std::isfinite(w[0]) && std::isfinite(w[1]) && std::isfinite(w[2]);
}

// The PC zombie's head: bone 3. At rest it is the highest (y -1510; legs 0/1 at 12, arms 11/12 at
// -1460/-1471); taking the "highest" at every bite sometimes chose a raised hand (30 degrees between
// "head" and origin, the PS1 does 1-3).
// The PS1 (80078A58) looks at the node of the part of type 2, which is the torso and not the head: PS1 RAM
// of 2026-10-22, 07/00 zombie, part 8 of type 2 with node 801920D8 (one before the head, step 0x50) at y
// -1142 and 27 units from the origin; head (types 0/1) at y -1574, legs (7-10) between -1000 and -600. On
// the PC the torso is bone 2, but its joint lies on the origin (no lateral offset) and the turn would not
// start: bone 3 is used, the first above the torso, which moves sideways like the PS1 node (turn of 7
// frames and sequence of 20, PS1 7 and 21).
// ASSUMPTION: it holds for all zombie types.
constexpr unsigned kHeadBone = 3;
bool ZombieHeadPoint(const uint8_t* o, float head[3]) { return BoneWorld(o, kHeadBone, head); }

void UpdateLunge(void* s, Entry& e) {
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) {
    e.phase = Phase::Idle;
    return;
  }
  float* accum = game::Field<float>(o, game::O::kAccum);
  accum[0] = accum[1] = accum[2] = 0.0f;  // only the PS1 movement
  const int selector = game::Read<int>(s, game::S::kSelector);
  if (selector != SelectorOf(e)) {
    if (e.applied) SetPhase(s, e, Phase::Idle, "selector changed by the game, back to the enemy logic");
    return;
  }
  const bool firstUpdate = !e.applied;
  e.applied = true;
  const bool completed = !firstUpdate && Q1Completed(o, e);
  const Geometry g = Measure(o, t);
  const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
  const float* pp = reinterpret_cast<const float*>(t + game::O::kPos);
  const float* zp = reinterpret_cast<const float*>(o + game::O::kPos);
  switch (e.phase) {
    case Phase::Lunge: {
      // Substate 0: forward at 4 x E+866 in the current direction.
      const int v = ps1::LungeSpeed(ZombieWalkSpeed(s, g.distance), ps1::kLungeSpeedFactor);
      MoveZombie(o, fw[0] * v, fw[2] * v);
      if (!completed) return;
      const bool cone = ps1::InBiteCone({pp[0], pp[2]}, PlayerForward(t), {zp[0], zp[2]});
      const bool gate = (!g_biter || g_biter == s) && !g_seq.s && !g_grab.s;
      const Geometry now = Measure(o, t);
      if (ps1::LungeGrabs(e.lungeBits, now.distance, cone, gate)) {
        g_biter = s;
        g_biterSeen = GetTickCount();
        g_seq.px = pp[0];  // the player stays still from here (80062DEC = 0)
        g_seq.pz = pp[2];
        const float off = AngleOf({zp[0] - pp[0], zp[2] - pp[2]}) - AngleOf(PlayerForward(t));
        e.slide = ps1::StartGrabSlide(AngleUnits(off));
        SetPhase(s, e, Phase::LungeGrab, "lunge hit: grab 1D");
      } else {
        SetPhase(s, e, Phase::LungeMiss, "lunge missed: 1F");
      }
      return;
    }
    case Phase::LungeMiss: {
      const int v = ps1::LungeSpeed(ZombieWalkSpeed(s, g.distance), ps1::kLungeMissSpeedFactor);
      MoveZombie(o, fw[0] * v, fw[2] * v);
      if (!completed) return;
      ps1::ResetOnChaseStart(e.lungeBand);  // 800802C8(E,4,4): state 4 again
      e.lungeBits = ps1::LungeBits();
      e.forceWalk = 4;  // ...with animation 4
      SetPhase(s, e, Phase::Idle, "after the missed lunge: state 4 with walk 04");
      return;
    }
    case Phase::LungeGrab:
    case Phase::LungeBite: {
      g_biter = s;
      g_biterSeen = GetTickCount();
      // Player still; zombie brought in front at 700, turning by 0x37 per frame.
      HoldPlayer(t, g_seq.px, g_seq.pz);
      const float* hp = reinterpret_cast<const float*>(t + game::O::kPos);
      const int offset = e.phase == Phase::LungeGrab ? ps1::StepGrabSlide(e.slide) : 0;
      const float a = AngleOf(PlayerForward(t)) + offset * ps1::kAngleUnit;
      float* zw = game::Field<float>(o, game::O::kPos);
      zw[0] = hp[0] + std::sin(a) * ps1::kLungeHoldDistance;
      zw[2] = hp[2] + std::cos(a) * ps1::kLungeHoldDistance;
      SetHeading(o, -std::sin(a), -std::cos(a));  // looks at the player
      if (e.phase == Phase::LungeGrab) {
        if (completed) {
          e.lungeBitten = false;
          SetPhase(s, e, Phase::LungeBite, "bite 1E");
        }
        return;
      }
      // 0x1E: damage and sequence at Q1 cursor = start + 4 (substate 6).
      const uint8_t* q = ExitTrack(o);
      const int start = q ? TrackStartCursor(q) : -1;
      if (!e.lungeBitten && start >= 0 && game::Read<int>(q, game::Q::kCursor) >= start + ps1::kLungeBiteFrame) {
        e.lungeBitten = true;
        *game::Field<int>(s, game::S::kHitArmed) = 0;
        g_hitPlayer(s, nullptr);
        CancelPcTurn(t);  // the sequence turns the view, at the PS1 step
        const float hx = g_seq.px, hz = g_seq.pz;
        StartBiteSequence(s, t);
        g_seq.px = hx;
        g_seq.pz = hz;
      }
      if (completed && !g_seq.s) SetPhase(s, e, Phase::Idle, "end of 1E, back to the enemy logic");
      return;
    }
    case Phase::Knockback: {
      // State 6 with 0x20: backwards by 5 x 20, 18, ..., 2 (80102D74 substate 1); then, every frame, the
      // roll for the backward fall before the end of the clip (substate 2: LAB_801030C0, then 80100728).
      // The same state 6 does the hit reactions (Ps1ZombieHit): different clip and push.
      const float push = ps1::NextBackPush(e.push) * g_config.backFallPush * e.pushSign;
      if (push != 0.0f) MoveZombie(o, -fw[0] * push, -fw[2] * push);
      if (RollBackFall(s, e)) return;
      if (completed) FinishKnockback(s, e);
      return;
    }
    default:
      return;
  }
}

void ResetBiteActor(void* s) {
  const bool owns = g_grab.s == s || g_seq.s == s || g_biter == s;
  g_bites.erase(s);
  if (g_grab.s == s) g_grab = Grab();
  if (g_seq.s == s) ResetBiteSequence();
  if (g_biter == s) g_biter = nullptr;
  if (owns) g_view = ViewTurnRun();
  g_backGrabCooldown = 0;  // 801047B4 resets the native family cooldown at initialisation.
  g_ownerMissed = 0;
}

void ResetBites() {
  g_bites.clear();
  g_biter = nullptr;
  g_biterSeen = 0;
  g_grab = Grab();
  ResetBiteSequence();
  g_view = ViewTurnRun();
  g_backGrabCooldown = 0;
  g_ownerMissed = 0;
}

}  // namespace sf::features::zombie
