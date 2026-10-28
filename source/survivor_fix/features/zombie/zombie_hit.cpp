// Ps1ZombieHit: a living zombie that is hit reacts like on the PS1 (ps1/hit_logic.h).
//
// PC (0043A8A0, decompiled, branch O+88): with the hit it rolls rng & 1. With 0, or if the current
// request S+1C is already 0B/0C/0D/0E/11/12/16, it removes health (O+11C -= O+9C), moves the track
// cursors back by 5 and ends the update asking again for S+1C (LAB_0043bbbe). Otherwise it does its
// reaction: S+20 = 2 (enemy logic still), request 0B/0C (10%: 0D/0E). So it reacts to about half of the hits;
// the PS1 (80101474) to about 12% (DuckStation 2026-10-21: 6 hits while walking, no reaction; a hit
// during 08 starts 0x20 again).
// Here, before the update, if the hit does not kill: S+1C = 0x0C makes the PC take the branch without
// reaction (health, blood and sound stay native), the mapper gets the previous request (HitMapRequest)
// and after the update S+1C goes back to what it was. Then the choice of 80101474 is applied with the
// Knockback phase (state 6 of 80102D74): reaction clip, push, end of clip -> enemy logic.
// TODO: the part hit of type 7-10 (+2 in the table) is not exposed by the PC and counts as 0 here; the
// hits during the bite stay native.
#include "zombie_shared.h"

#include <mmsystem.h>

namespace sf::features::zombie {

namespace {

void* g_hitSuppress = nullptr;  // zombie whose update takes the PC branch without reaction
int g_hitSavedRequest = 0;

constexpr int kNoReactionRequest = 0x0C;  // one of the requests that 0043A8A0 does not interrupt
constexpr unsigned kHeadBone = 3;
constexpr float kLethalHeadRadius = 250.0f;

// A short embedded wet pop. The shipped cue tried previously is only a few
// milliseconds long and disappears under the simultaneous death voice.
void PlayHeadPop() {
  // Use RE2 PC enemy00.sap entry 0002 when the packaged reference cue is
  // available. Keep the generated cue as a safe fallback for older installs.
  if (PlaySoundA("FIXRES\\headpop.wav", nullptr,
                 SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) return;
  static std::vector<uint8_t> wave;
  if (wave.empty()) {
    constexpr uint32_t rate = 11025;
    constexpr uint32_t samples = rate * 18 / 100;
    constexpr uint32_t bytes = samples * 2;
    wave.resize(44 + bytes);
    auto w16 = [&](size_t at, uint16_t v) { wave[at] = uint8_t(v); wave[at + 1] = uint8_t(v >> 8); };
    auto w32 = [&](size_t at, uint32_t v) {
      wave[at] = uint8_t(v); wave[at + 1] = uint8_t(v >> 8);
      wave[at + 2] = uint8_t(v >> 16); wave[at + 3] = uint8_t(v >> 24);
    };
    std::memcpy(wave.data(), "RIFF", 4); w32(4, 36 + bytes);
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
    w32(24, rate); w32(28, rate * 2); w16(32, 2); w16(34, 16);
    std::memcpy(wave.data() + 36, "data", 4); w32(40, bytes);
    uint32_t noise = 0x51F15EEDu;
    for (uint32_t i = 0; i < samples; ++i) {
      noise = noise * 1664525u + 1013904223u;
      const float t = float(i) / rate;
      const float decay = std::exp(-t * 25.0f);
      const float body = std::sin(6.2831853f * (115.0f - 230.0f * t) * t) * 0.72f;
      const float grit = (float(int(noise >> 16) - 32768) / 32768.0f) * 0.38f;
      const int value = int((body + grit) * decay * 28000.0f);
      const int16_t sample = int16_t((std::max)(-32768, (std::min)(32767, value)));
      w16(44 + i * 2, uint16_t(sample));
    }
  }
  PlaySoundA(reinterpret_cast<LPCSTR>(wave.data()), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

bool IsLethalHeadHit(const uint8_t* o) {
  float head[3];
  if (!o || !BoneWorld(o, kHeadBone, head)) return false;
  const float* hit = reinterpret_cast<const float*>(o + game::O::kHitPos);
  const float dx = hit[0] - head[0], dy = hit[1] - head[1], dz = hit[2] - head[2];
  return dx * dx + dy * dy + dz * dz < kLethalHeadRadius * kLethalHeadRadius;
}

// Number of sequences of the model (track 0, Q+0C): the mapper takes the selector modulo this
// number, so a clip beyond it does not exist for this zombie.
bool ClipExists(const uint8_t* o, int clip) {
  auto* m = game::Read<uint8_t*>(o, game::O::kModel);
  if (!m) return false;
  auto* tracks = game::Read<uint8_t*>(m, game::M::kTracks);
  if (!tracks || game::Read<unsigned>(m, game::M::kTrackCount) == 0) return false;
  return clip >= 0 && clip < game::Read<int>(tracks, game::Q::kCount);
}

}  // namespace

int SelectorOf(const Entry& e) {
  if (e.phase == Phase::Knockback && e.reactClip) return e.reactClip;
  if (e.phase == Phase::Death) return e.deathClip;
  if (e.phase == Phase::Feed) return kFeedClip;
  return PhaseSelector(e.phase);
}

bool HitRewindActive() { return g_hitSuppress != nullptr; }

int HitMapRequest(void* s, int request) {
  if (s != g_hitSuppress || request != kNoReactionRequest) return request;
  *game::Field<int>(s, game::S::kRequest) = g_hitSavedRequest;
  return g_hitSavedRequest;
}

HitGate HitBefore(void* s) {
  HitGate g;
  const int type = game::Read<int>(s, game::S::kType);
  const bool mappedZombie = ps1::IsPcZombie(type);
  if (!mappedZombie && !(g_config.lethalZombieHeadshots && IsHeadshotZombie(type))) return g;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t) || game::Read<uint8_t>(o, game::O::kHitFlag) == 0) return g;
  const float hp = game::Read<float>(o, game::O::kHealth);
  if (g_config.lethalZombieHeadshots && hp > 0.0f && IsLethalHeadHit(o)) {
    Entry& e = g_entries[s];
    const float incomingDamage = game::Read<float>(o, game::O::kHitDamage);
    // A naturally lethal high-powered hit (shotgun/magnum) pops immediately.
    // Weaker weapons retain the requested two-headshot minimum.
    if (++e.headshotCount >= 2 || incomingDamage >= hp) {
      *game::Field<float>(o, game::O::kHitDamage) = hp;
      e.lethalHeadshot = true;
      PlayHeadPop();
      SpawnHeadBurst(s, o);
      Log("ZombieHeadshots: S=%p lethal head hit %u damage=%.1f hp=%.1f; head removed",
          s, e.headshotCount, incomingDamage, hp);
    } else {
      *game::Field<float>(o, game::O::kHitDamage) =
          (std::min)(incomingDamage, (std::max)(0.0f, hp - 1.0f));
      Log("ZombieHeadshots: S=%p head hit 1; guaranteed survivor", s);
    }
  }
  // IDs 3 and 7 are zombies and can use the self-contained headshot feature,
  // but must not enter the PS1 hit-reaction mapper whose clips are unknown.
  if (!mappedZombie) return g;
  const float damage = game::Read<float>(o, game::O::kHitDamage);
  if (!g_config.ps1ZombieHit) return g;
  if (!(hp - damage > 0.0f)) return g;  // deadly hit: native death
  Entry& e = g_entries[s];
  const int selector = game::Read<int>(s, game::S::kSelector);
  const bool knockbackBit = (e.phase == Phase::Knockback && !e.reactClip) || e.phase == Phase::BackFall;
  g.kind = ps1::ClassifyHit(selector, knockbackBit);
  if (IsBiting(s) || e.phase == Phase::LungeGrab || e.phase == Phase::LungeBite) {
    g.kind = ps1::HitKind::None;  // TODO: hit during the bite (controls blocked, rare)
  }
  const Geometry geo = Measure(o, t);
  g.distance = geo.distance;
  const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
  g.behind = fw[0] * geo.toX + fw[2] * geo.toZ < 0.0f;  // E+32B 2-5: shooter behind
  if (g.kind == ps1::HitKind::Table) {
    g.rng = NextRngByte();
    g.clip = ps1::HitTableClip(ps1::HitTableRow(g.rng), false, g.behind);
    if (g.clip >= 0 && !ClipExists(o, g.clip)) g.clip = -1;
    if (g.clip < 0) g.kind = ps1::HitKind::None;
  }
  // The hit during a finished reaction (clip 06/08/0C/0x15 still on screen, phase already back with the
  // enemy logic): there is no state 6 any more, so no "reaction in progress".
  if (g.kind == ps1::HitKind::Keep && !(e.phase == Phase::Knockback && e.reactClip)) {
    g.kind = ps1::HitKind::None;
  }
  if (auto* m = game::Read<uint8_t*>(o, game::O::kModel)) {
    auto* q = game::Read<uint8_t*>(m, game::M::kTracks);
    const unsigned n = game::Read<unsigned>(m, game::M::kTrackCount);
    for (unsigned k = 0; q && k < n && g.tracks < HitGate::kMaxTracks; ++k, ++g.tracks) {
      g.q40[g.tracks] = game::Read<int>(q + k * game::M::kTrackSize, 0x40);
      g.q44[g.tracks] = game::Read<int>(q + k * game::M::kTrackSize, game::Q::kCursor);
    }
  }
  g.active = true;
  g.selector = selector;
  g.savedRequest = game::Read<int>(s, game::S::kRequest);
  *game::Field<int>(s, game::S::kRequest) = kNoReactionRequest;
  g_hitSuppress = s;
  g_hitSavedRequest = g.savedRequest;
  return g;
}

void HitAfter(void* s, const HitGate& g) {
  if (!g.active) return;
  g_hitSuppress = nullptr;
  *game::Field<int>(s, game::S::kRequest) = g.savedRequest;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return;
  if (game::Read<uint8_t>(o, game::O::kHitFlag) != 0) return;  // hit not consumed by the update
  // The branch without reaction (0043A8A0) moved the cursors back by 5: if the clip is the same they are
  // restored, otherwise the end of clip (Q1Completed) sees the cursor going back and closes the running
  // reaction (0x15 interrupted after 0.36 s, zombie half inside the floor).
  if (game::Read<int>(s, game::S::kSelector) == g.selector) {
    if (auto* m = game::Read<uint8_t*>(o, game::O::kModel)) {
      auto* q = game::Read<uint8_t*>(m, game::M::kTracks);
      for (int k = 0; q && k < g.tracks; ++k) {
        *game::Field<int>(q + k * game::M::kTrackSize, 0x40) = g.q40[k];
        *game::Field<int>(q + k * game::M::kTrackSize, game::Q::kCursor) = g.q44[k];
      }
    }
  }
  Entry& e = g_entries[s];
  const float sign = g.behind ? -1.0f : 1.0f;
  ps1::HitKind kind = g.kind;
  // The reaction may have ended in the mapper of this same update (FinishKnockback): then there is no
  // state 6 to go on with.
  if (kind == ps1::HitKind::Keep && !(e.phase == Phase::Knockback && e.reactClip)) kind = ps1::HitKind::None;
  switch (kind) {
    case ps1::HitKind::Knockback:
      e.reactClip = 0;
      e.slideP10 = ps1::HitSlide(0x20, e.slideBase, e.slideP10);
      SetPhase(s, e, Phase::Knockback, "hit in state 6: 0x20 again");
      ps1::StartKnockbackPush(e.push, static_cast<float>(e.slideP10 / 100));
      break;
    case ps1::HitKind::Table:
      e.reactClip = g.clip;
      e.slideBase = ps1::HitSlideBase(g.distance);
      e.armsRoll = e.slideBase;  // 80101474: P+0C = 1000/500/200
      e.slideP10 = ps1::HitSlide(g.clip, e.slideBase, 0);
      SetPhase(s, e, Phase::Knockback, "hit reaction");
      if (ps1::HitPushes(g.clip)) {
        ps1::StartKnockbackPush(e.push, static_cast<float>(e.slideP10 / 100));
      } else {
        e.push = ps1::BackPush();
      }
      break;
    case ps1::HitKind::Keep:
      // 801012CC -> 800802C8(E,6): substate 0 again, P+10 adds up, P+14 restarts if it was over.
      e.slideP10 = ps1::HitSlide(e.reactClip, e.slideBase, e.slideP10);
      if (ps1::HitPushes(e.reactClip)) {
        if (e.push.strength <= 0) ps1::StartKnockbackPush(e.push);
        e.push.magnitude = static_cast<float>(e.slideP10 / 100);
        e.push.delay = 1;  // the substate 0 frame does not push
      }
      break;
    default:
      break;
  }
  if (kind == ps1::HitKind::Knockback || kind == ps1::HitKind::Table) {
    e.pushSign = sign;
    *game::Field<int>(s, game::S::kSelector) = -1;     // the clip starts again from the beginning
    *game::Field<uint8_t>(s, game::S::kThreshold) = 0;  // and interrupts the running one
  }
  if (kind == ps1::HitKind::None) {
    e.armsRoll = 0;  // 80101474 row >= 0x10: P+0C = 0, the arms roll is done again
    // 80101474 row 4 (also the clips without a case): E+368 = +/-0x180, the torso bend (Ps1ZombiePose).
    if (g.kind != ps1::HitKind::Keep) PoseOnHit(s, g.behind);
  }
}

}  // namespace sf::features::zombie
