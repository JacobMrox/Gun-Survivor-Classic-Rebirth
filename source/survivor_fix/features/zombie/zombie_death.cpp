// Ps1ZombieDeath: the zombie's death like on the PS1 (ps1/death_logic.h, 80101700 and 801032CC).
// Native PC: with health at 0 request 0F/0x10 always plays 07 (31 updates, speed 4.0) -> 08 (5) -> 0x12
// (118, speed 1.0) -> 09, from any side and state. The PS1 chooses from the running clip and, standing, from
// the part hit: torso 0x0B -> 0x0C -> half of the time 0x13, head 08 -> half of the time 0x12 (from behind
// 0x0C), on the ground 09; all at 1x, no movement, the last pose stays (DuckStation 2026-10-22).
// On the PC the 14 tracks of the model loop, each with its own length: track 1 of 08 goes back to the
// start after 17 updates while the others go on (zombie stuck halfway through the fall). Here every track,
// when it goes back to the start, stays on its last pose (the whole structure, keys +0x40/+0x44 and
// fraction: with the keys alone the pose wobbles) and the clip ends when all of them have ended.
// On the ground, like the PC death at the end of the clip with S+4C (0043A9AA): 00464FA0 counts the killed
// enemy and S+5C = 1 creates the blood pool (0043C951, BLOODX1/2 instead of the shadow). The follow-up
// 0x12 / 0x13 loops like on the PS1; without a follow-up the last pose stays.
// TODO: the PC 0x0B has 22 keys against the 17 of the PS1 (44 updates against 33 frames, same movement).
// The PC holds every key a whole number of updates by the speed applied when the clip starts (2 at 2.93,
// still 2 at 3.91, 1 at 5.86; changing S+30 once the clip runs does not count): 33 cannot be reached
// without holding one update in three (a jerk). It stays at 1x, 44 updates.
// ASSUMPTION: the PS1 head (part of type 1, node at 1578 height, box 64) is the hit point O+90 within
// kHeadRadius of PC bone 3; the PC does not tell the arms (types 3-4) apart and they count as torso (on the
// PS1 87.5% of the arms give 0x0B anyway).
// TODO: the part of type 0 (box 24 on the head) kills at once for 80102224 whatever the health and
// 80101700 goes to state 9 with clip 3.
#include "zombie_shared.h"

#include <algorithm>
#include <vector>

#include "../../ps1/death_logic.h"

namespace sf::features::zombie {

namespace {

constexpr unsigned kHeadBone = 3;
constexpr float kHeadRadius = 250.0f;
constexpr size_t kPool = 0x5C;     // S+5C: > 0 draws the pool and makes it grow (0043C951)
constexpr int kStaticUpdates = 6;  // a track still for this many updates has no keys to run through
float DeathSpeed(int) { return g_config.biteSpeed; }  // 801032CC: all at 1x

using CountKillFn = void(__fastcall*)(void* stats, void* edx);
const auto g_countKill = reinterpret_cast<CountKillFn>(game::kCountKill);

struct Death {
  bool native = false;  // 80101700 keeps the running clip: PC death for this request
  int clips[3] = {0, 0, 0};
  int count = 0;
  int step = 0;
  bool rolled = false;
  bool frozen = false;
  bool pooled = false;
  bool looping = false;  // follow-up on the ground that loops
  DWORD seen = 0;
  // Per track: structure of the previous update, the one held at the end of the track, cursor and state.
  int tracks = 0;
  std::vector<uint8_t> prev, held;
  std::vector<int> cursor, still;
  std::vector<bool> moved, done;
};
std::unordered_map<void*, Death> g_deaths;
bool g_deathReady = false;  // bytes of 00464FA0 and of its call verified

bool ClipExists(const uint8_t* o, int clip) {
  auto* m = game::Read<uint8_t*>(o, game::O::kModel);
  if (!m) return false;
  auto* tracks = game::Read<uint8_t*>(m, game::M::kTracks);
  if (!tracks || game::Read<unsigned>(m, game::M::kTrackCount) == 0) return false;
  return clip >= 0 && clip < game::Read<int>(tracks, game::Q::kCount);
}

uint8_t* Tracks(const uint8_t* o, unsigned& n) {
  auto* m = game::Read<uint8_t*>(o, game::O::kModel);
  auto* q = m ? game::Read<uint8_t*>(m, game::M::kTracks) : nullptr;
  n = m ? game::Read<unsigned>(m, game::M::kTrackCount) : 0;
  return q && n > 0 && n <= 64 ? q : nullptr;
}

void StartClipTracks(const uint8_t* o, Death& d) {
  unsigned n;
  const uint8_t* q = Tracks(o, n);
  d.tracks = q ? static_cast<int>(n) : 0;
  d.prev.assign(q, q ? q + n * game::M::kTrackSize : q);
  d.held.assign(d.prev.size(), 0);
  d.cursor.assign(d.tracks, -1);
  d.still.assign(d.tracks, 0);
  d.moved.assign(d.tracks, false);
  d.done.assign(d.tracks, false);
}

// The finished tracks stay on their last pose.
void HoldDone(uint8_t* o, const Death& d) {
  unsigned n;
  uint8_t* q = Tracks(o, n);
  if (!q || static_cast<int>(n) != d.tracks) return;
  for (int k = 0; k < d.tracks; ++k) {
    if (d.done[k] || d.frozen) {
      std::memcpy(q + k * game::M::kTrackSize, d.held.data() + k * game::M::kTrackSize, game::M::kTrackSize);
    }
  }
}

// After the clip's update: a track whose cursor goes back has ended and the one of the previous update is
// held. True when all the tracks that run have ended; with `still` also when none runs (the PC 0x0D is a
// single pose: the body never reached the ground).
bool TracksFinished(uint8_t* o, Death& d, bool still) {
  unsigned n;
  uint8_t* q = Tracks(o, n);
  if (!q || static_cast<int>(n) != d.tracks) return false;
  bool all = true, any = false;
  for (int k = 0; k < d.tracks; ++k) {
    uint8_t* tk = q + k * game::M::kTrackSize;
    const int c = game::Read<int>(tk, game::Q::kCursor);
    if (!d.done[k] && d.cursor[k] >= 0) {
      if (c < d.cursor[k] && d.moved[k]) {
        d.done[k] = true;
        std::memcpy(d.held.data() + k * game::M::kTrackSize, d.prev.data() + k * game::M::kTrackSize,
                    game::M::kTrackSize);
      } else if (c > d.cursor[k]) {
        d.moved[k] = true;
        d.still[k] = 0;
      } else if (++d.still[k] >= kStaticUpdates) {
        d.still[k] = kStaticUpdates;
        if (d.moved[k]) {
          // Advanced and then still on the last key: the PC holds the clip at its end (0x0D has 2 keys and
          // stays on the second; the body never reached the ground and still blocked the way).
          d.done[k] = true;
          std::memcpy(d.held.data() + k * game::M::kTrackSize, tk, game::M::kTrackSize);
        }  // without keys to run through: it does not count
      }
    }
    if (!d.done[k]) {
      d.cursor[k] = c;
      if (d.moved[k] || d.still[k] < kStaticUpdates) all = false;
    }
    any = any || d.done[k];
  }
  HoldDone(o, d);
  d.prev.assign(q, q + n * game::M::kTrackSize);
  return all && (any || still);
}

void NextClip(void* s, Entry& e, Death& d, const char* why) {
  ++d.step;
  e.applied = false;
  e.updates = 0;
  e.q1Cursor = -1;
  e.q1Stall = 0;
  e.q1Advanced = false;
  e.phaseBlend = ps1::kDeathSecondBlend;  // 80080334(E, clip, 2)
  e.deathClip = d.clips[d.step];
  Log("Ps1ZombieDeath: S=%p %s: %02X", s, why, d.clips[d.step]);
}

// On the ground: what the PC death does at the end of the clip (0043A9AA..0043A9C3), once.
void Grounded(void* s, Death& d) {
  if (d.pooled) return;
  d.pooled = true;
  if (game::Read<int>(s, kPool) != 0) return;
  if (auto* stats = *reinterpret_cast<void**>(game::kGameStats)) g_countKill(stats, nullptr);
  *game::Field<int>(s, kPool) = 1;
}

ps1::DeathPart PartOf(const uint8_t* o) {
  const float* hit = reinterpret_cast<const float*>(o + game::O::kHitPos);
  float head[3];
  if (!BoneWorld(o, kHeadBone, head)) return ps1::DeathPart::Body;
  const float dx = hit[0] - head[0], dy = hit[1] - head[1], dz = hit[2] - head[2];
  return dx * dx + dy * dy + dz * dz < kHeadRadius * kHeadRadius ? ps1::DeathPart::Head : ps1::DeathPart::Body;
}

}  // namespace

bool DeathMap(void* s, Entry& e, int request, int& result) {
  if (!g_config.ps1ZombieDeath || !g_deathReady) return false;
  if (e.phase == Phase::Death) {
    HoldSequence(s, e, result);
    *game::Field<float>(s, game::S::kNextSpeed) = DeathSpeed(e.deathClip);
    return true;
  }
  auto known = g_deaths.find(s);
  if (!IsDeath(request)) {
    if (known != g_deaths.end()) g_deaths.erase(known);
    return false;
  }
  if (known != g_deaths.end() && known->second.native) return false;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return false;
  const int current = game::Read<int>(s, game::S::kSelector);
  const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
  const Geometry g = Measure(o, t);
  const bool behind = fw[0] * g.toX + fw[2] * g.toZ < 0.0f;  // E+32B 2-5
  const ps1::DeathPart part = PartOf(o);
  const ps1::DeathPlan plan = ps1::PickDeath(current, part, behind, NextRngByte(), NextRngByte());
  Death& d = g_deaths[s];
  d = Death{};
  d.seen = GetTickCount();
  if (plan.count == 0) {  // 80101700 keeps the running clip: PC death
    d.native = true;
    return false;
  }
  for (int i = 0; i < plan.count; ++i) {
    if (!ClipExists(o, plan.clips[i])) {  // clip missing in the PC model: native death
      d.native = true;
      return false;
    }
  }
  for (int i = 0; i < plan.count; ++i) d.clips[i] = plan.clips[i];
  d.count = plan.count;
  e.reactClip = 0;
  SetPhase(s, e, Phase::Death, "PS1 death");
  e.phaseBlend = plan.blend;
  e.deathClip = d.clips[0];
  HoldSequence(s, e, result);
  *game::Field<float>(s, game::S::kNextSpeed) = DeathSpeed(e.deathClip);
  return true;
}

// After the native update: end of the clip (all tracks), follow-up, pose on the ground, held.
void UpdateDeath(void* s, Entry& e) {
  uint8_t *o, *t;
  auto it = g_deaths.find(s);
  if (it == g_deaths.end() || !GetObjects(s, o, t)) return;
  Death& d = it->second;
  d.seen = GetTickCount();
  float* accum = game::Field<float>(o, game::O::kAccum);
  accum[0] = accum[1] = accum[2] = 0.0f;  // 801032CC does not move the zombie
  if (d.frozen) {
    HoldDone(o, d);
    return;
  }
  if (d.looping) return;
  if (game::Read<int>(s, game::S::kSelector) != d.clips[d.step]) return;  // the clip starts at the next update
  if (!e.applied) {
    e.applied = true;
    StartClipTracks(o, d);
    return;
  }
  if (ps1::DeathLoops(d.clips[d.step]) && d.rolled) {
    d.looping = true;  // 801032CC substate 4: 0x12 / 0x13 on the ground loops
    return;
  }
  // A still clip counts as finished only after the blend (E+8B0 drops by 0x10 per frame, 8007F6AC).
  const int blendUpdates = (e.phaseBlend > 0 ? e.phaseBlend / 0x10 : 0) + kStaticUpdates;
  if (!TracksFinished(o, d, ++e.updates > blendUpdates)) return;
  if (d.step + 1 < d.count) {
    NextClip(s, e, d, "follow-up clip");
    return;
  }
  if (!d.rolled) {
    d.rolled = true;
    Grounded(s, d);  // 801032CC substate 3: from here the body is on the ground
    const int second = ps1::DeathSecond(d.clips[d.step], NextRngByte());
    if (second >= 0 && ClipExists(o, second) && d.count < 3) {
      d.clips[d.count++] = second;
      NextClip(s, e, d, "pose on the ground (801032CC)");
      return;
    }
  }
  d.frozen = true;
  for (int k = 0; k < d.tracks; ++k) {  // the tracks without keys to run through stay as they are
    if (!d.done[k]) {
      std::memcpy(d.held.data() + k * game::M::kTrackSize, d.prev.data() + k * game::M::kTrackSize,
                  game::M::kTrackSize);
    }
  }
  std::fill(d.done.begin(), d.done.end(), true);
  HoldDone(o, d);
}

// After every draw of the model (DrawModelDetour): the finished tracks do not advance. Only for the zombies
// updated recently (the entries of a room left behind point to freed memory).
void DeathAfterDraw(void* s) {
  auto it = g_deaths.find(s);
  uint8_t *o, *t;
  if (it == g_deaths.end() || it->second.native || GetTickCount() - it->second.seen > kAliveMs ||
      !GetObjects(s, o, t)) {
    return;
  }
  HoldDone(o, it->second);
}

// 801032CC substate 3: E+320 |= 1 when entering the pose on the ground (after the roll of 0x12/0x13), and
// 80081C80 / 80081618 no longer touch the body.
bool DeathLying(void* s) {
  auto it = g_deaths.find(s);
  return it != g_deaths.end() && !it->second.native && (it->second.rolled || it->second.frozen);
}

void ResetDeathActor(void* s) { g_deaths.erase(s); }
void ResetDeaths() { g_deaths.clear(); }

bool InstallZombieDeath() {
  if (!g_config.ps1ZombieDeath) return false;
  g_deathReady = game::CheckBytes({game::kCountKill, game::kCountKillBytes, sizeof(game::kCountKillBytes)}) &&
                 game::CheckBytes({game::kCountKillCall, game::kCountKillCallBytes, sizeof(game::kCountKillCallBytes)});
  if (!g_deathReady) {
    Log("Ps1ZombieDeath: unexpected bytes at %08X, PC death unchanged", static_cast<unsigned>(game::kCountKill));
  }
  return g_deathReady;
}

}  // namespace sf::features::zombie
