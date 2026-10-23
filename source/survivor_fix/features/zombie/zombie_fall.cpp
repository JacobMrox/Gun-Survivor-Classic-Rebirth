// PS1 zombie stumble/fall: 23 -> 10 -> 11, like 80102828/80103C6C, and the backward fall
// 0x20 -> 08 -> 0E -> 0F, like 80102D74/80104108.
//
// PS1 logic: ps1/stumble_logic.* and ps1/backfall_logic.* (bands, rearming and sector verified in
// the MIPS code). PC hooks (addresses and ABI in game.h, verified in Ghidra):
//   - mapper 00436E40: during the chase (requests 2/3/4) it evaluates the PS1 attempt and, if it
//     succeeds, returns 0x23 instead of the walk. While the sequence runs it returns the wanted
//     selector: for type 0 the game "keeps" the same selector without applying it again. The
//     transitions (exit at start+8, end of 10 and 11) are decided here too, in the same update in
//     which the game records the completion.
//   - update 0043A8A0: fallback for the transitions when the enemy logic does not ask for animations, and
//     it clears the accumulated movement, so the zombie does not slide while on the ground.
//
// ASSUMPTION: PC position units equal the PS1 ones (1:1 scale).
// Forward direction: third row of O's matrix (game.h, O::kForward), verified by reading memory in game.
// ASSUMPTION: T (C+14) is the player.
// ASSUMPTION: cursor Q+44 counts the commands like the PS1 cursor Q1+16 (same streams and pairs).
// TODO: close outcome 24 (grab) and gate 80100628: for now the close outcome gives the zombie back
//   to the PC enemy logic, which decides the attack.
//
// Speed and threshold: by default the same the PC mapper uses for its own falls and get-ups
// (0C/13 at 00437021, 10/11 at 0043721C): S+40 = 2.0, S+3C = 1. For 23 the threshold is 0,
// otherwise 10 would not start at start+8.
//
// Backward fall (Ps1ZombieBackFall): while 0x20 runs (on the PC one of the gestures 1B-21 of request
// 08), at command start+10 an 80% roll goes on to 08 -> 0E -> 0F like 80102D74/80104108. The
// backward push of the first 10 updates goes through 0045FD70, the game's local accumulator, so
// the collisions stay.
// ASSUMPTION: in the accumulator local +Z is "forward" (the walk accumulates (0,0,S+74) in
//   0043A8A0): the push uses -Z.
// ASSUMPTION: on the PS1 the sign of the push depends on +0x32B (direction); here it is always
//   backwards, consistent with the backward fall.
#include "zombie_shared.h"

#include <algorithm>

namespace sf::features::zombie {

// Phase transitions: exit of 23 at start+8, completions of 10 and 11.
// They are evaluated in the mapper, which in 0043A8A0 comes after the completion loop: in the
// same update the cursor is already back at the start of the finished sequence, so the next one
// must be applied at once, otherwise the initial pose shows for one frame (the jerk at the end of
// the get-up). The update is the fallback when the enemy logic does not ask for animations.
constexpr int kFallExitDelay = 2;  // frames of 0x23 after the outcome (key 9)

void Step(void* s, Entry& e) {
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) {
    e.phase = Phase::Idle;
    return;
  }
  const int selector = game::Read<int>(s, game::S::kSelector);
  if (selector != PhaseSelector(e.phase)) {
    if (e.applied) SetPhase(s, e, Phase::Idle, "selector changed by the game, back to the enemy logic");
    return;  // not applied yet: the mapper keeps asking for it
  }
  if (!e.applied) {
    e.applied = true;
    return;  // just applied: no completions to evaluate
  }

  const bool completed = game::Read<int>(s, game::S::kLoops) > 0;
  if (IsBackChain(e.phase)) {
    if (!Q1Completed(o, e)) return;
    if (e.phase == Phase::BackFall) {
      SetPhase(s, e, Phase::Supine, "on the ground: 0E");
    } else if (e.phase == Phase::Supine) {
      SetPhase(s, e, Phase::SitUp, "getting up 0F");
    } else {
      e.nextBlend = ps1::kBlendStateChange;  // 80104108: 80080268, E+8B0 = 0x180
      SetPhase(s, e, Phase::Idle, "standing after the backward fall, back to the enemy logic");
    }
    return;
  }
  if (e.phase == Phase::Fall) {
    const uint8_t* q = ExitTrack(o);
    const int start = q ? TrackStartCursor(q) : -1;
    const bool reached = q && start >= 0 && game::Read<int>(q, game::Q::kCursor) >= start + g_config.exitFrame;
    if (!reached && !completed) return;
    if (e.fallExitAt < 0) {
      const Geometry g = Measure(o, t);
      // E+320 bit 0x40: zombie in the sector in front of the player (800824B0).
      const bool front = InSectorOf(t, -g.toX, -g.toZ);
      // ASSUMPTION: gate 80100628 always passes (see the TODO at the top).
      e.fallExitContact = ps1::DecideFallExit(g.distance, front, true) == ps1::FallExit::Contact;
      e.fallExitAt = e.updates + kFallExitDelay;
    }
    // 80103C6C decides at start+8; then 800802C8(E,4,0x10) changes clip at the next key: 0x23 goes on for
    // 2 more frames (DuckStation 2026-10-22: 15 frames up to +8, 18 in all; the PC switched at once, 15).
    if (e.updates < e.fallExitAt && !completed) return;
    e.fallExitAt = -1;
    if (e.fallExitContact) {
      SetPhase(s, e, Phase::Idle, "close outcome, back to the enemy logic (24 not ported)");
    } else {
      SetPhase(s, e, Phase::GetUp1, "outcome: getting up 10");
      e.phaseBlend = ps1::kBlendStateChange;  // 80103C6C substate 1: 800802C8(E,4,0x10), 24 frames
    }
  } else if (completed) {
    if (e.phase == Phase::GetUp1) {
      SetPhase(s, e, Phase::GetUp2, "getting up 11");
    } else {
      // On the PS1 10 goes back to state 4 (800802C8(E,4,10)): the commit clears E+31C and 80102828
      // restarts from band 0 with the attempt rearmed.
      ps1::ResetOnChaseStart(e.state);
      e.nextBlend = ps1::kBlendStateChange;  // 80104108: 80080268, E+8B0 = 0x180
      SetPhase(s, e, Phase::Idle, "standing, back to the enemy logic");
    }
  }
}

// 80102D74: with 0x20 running, at Q1 cursor start+10 it rolls for 08.
bool TryStartBackFall(void* s, Entry& e, int& result) {
  if (game::Read<int>(s, game::S::kSelector) != ps1::kSelBackFallTrigger) {
    e.backRolled = false;
    return false;
  }
  if (e.backRolled) return false;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return false;
  const uint8_t* q = ExitTrack(o);
  const int start = q ? TrackStartCursor(q) : -1;
  if (start < 0 || game::Read<int>(q, game::Q::kCursor) < start + g_config.backFallFrame) {
    return false;
  }
  e.backRolled = true;
  if (!ps1::ShouldStartBackFall(NextRngByte())) return false;
  SetPhase(s, e, Phase::BackFall, "backward fall 08");
  ps1::StartBackPush(e.push);
  // 0x20 may have a completion threshold (S+2C): on the PS1 08 starts at start+10 anyway, so it is
  // made interruptible.
  *game::Field<uint8_t>(s, game::S::kThreshold) = 0;
  HoldSequence(s, e, result);
  return true;
}

// 80102D74 substate 2 (LAB_801030C0), on every update of 0x20 after the bite: with the Q1 cursor
// exactly at start+10 it rolls (rng < 0xCC), and rolls again the next update if the cursor is still
// there (ps1::IsBackFallCheckFrame). Returns true if 08 starts: the mapper applies it at the next
// request (IsBackChain -> HoldSequence).
bool RollBackFall(void* s, Entry& e) {
  if (game::Read<int>(s, game::S::kSelector) != ps1::kSelBackFallTrigger) return false;
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return false;
  const uint8_t* q = ExitTrack(o);
  const int start = q ? TrackStartCursor(q) : -1;
  const int cursor = q ? game::Read<int>(q, game::Q::kCursor) : -1;
  if (!ps1::IsBackFallCheckFrame(cursor, start, g_config.backFallFrame)) return false;
  if (!ps1::ShouldStartBackFall(NextRngByte())) return false;
  SetPhase(s, e, Phase::BackFall, "backward fall 08 after the bite");
  ps1::StartBackPush(e.push);
  *game::Field<uint8_t>(s, game::S::kThreshold) = 0;  // 08 interrupts 0x20
  return true;
}

int MapAnimInner(void* s, void* edx, int request) {
  int result = g_mapAnim(s, edx, request);
  if (!g_config.ps1ZombieStumble && !g_config.ps1ZombieBackFall && !g_config.ps1ZombieNavigation &&
      !g_config.ps1ZombieBite && !g_config.ps1ZombieDeath) {
    return result;
  }
  if (!ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) return result;

  Entry& e = g_entries[s];
  if (DeathMap(s, e, request, result)) {  // before the phases, which would go back to the enemy logic on death
    e.prevRequest = request;
    return result;
  }
  const bool locomotion = IsLocomotion(request);
  if (e.phase == Phase::Feed) {
    HoldSequence(s, e, result);
    e.prevRequest = request;
    return result;
  }
  if (IsLungePhase(e.phase)) {
    // State 0xB and reaction 0x20: on the PS1 the enemy logic does not change clip until they end.
    if (IsDeath(request)) {
      e.phase = Phase::Idle;
    } else if (e.phase == Phase::Knockback && ExitTrackWrapped(s, e)) {
      // End of 0x20 or of the reaction in the same update in which the track goes back to the start,
      // like Step for the backward fall: otherwise the initial pose of the clip shows for one frame
      // (while falling they seemed to reset).
      FinishKnockback(s, e);
      if (e.phase != Phase::Idle) HoldSequence(s, e, result);
    } else {
      // 0x20 after the bite rolls for 08 in the update (RollBackFall), once per frame.
      HoldSequence(s, e, result);
    }
    e.prevRequest = request;
    return result;
  }
  if (e.phase == Phase::Idle && locomotion && g_config.ps1ZombieBite && TryStartLunge(s, e, request, result)) {
    e.prevRequest = request;
    return result;
  }
  if (IsBackChain(e.phase)) {
    if (IsDeath(request)) {
      e.phase = Phase::Idle;
    } else {
      // During the backward fall the enemy logic still asks for 08 (close action) or locomotion: the
      // sequence is kept, like on the PS1 in state 6.
      Step(s, e);
      if (e.phase != Phase::Idle) HoldSequence(s, e, result);
    }
    e.prevRequest = request;
    return result;
  }
  if (e.phase == Phase::Idle && g_config.ps1ZombieBackFall && TryStartBackFall(s, e, result)) {
    e.prevRequest = request;
    return result;
  }
  if (!g_config.ps1ZombieStumble) {
    e.prevRequest = request;
    return result;
  }
  if (e.phase != Phase::Idle) {
    if (locomotion) {
      Step(s, e);
      // Back to Idle: the mapper's result stays (for type 0, with the sequence complete, a walk
      // 01-04 chosen by the game).
      if (e.phase != Phase::Idle) HoldSequence(s, e, result);
    } else {
      // Damage, death or another action: the game decides.
      e.phase = Phase::Idle;
    }
  } else if (locomotion) {
    uint8_t *o, *t;
    if (GetObjects(s, o, t)) {
      if (!IsLocomotion(e.prevRequest)) ps1::ResetOnChaseStart(e.state);
      const Geometry g = Measure(o, t);
      ps1::UpdateBand(e.state, g.distance);
      const bool inSector = InSectorOf(o, g.toX, g.toZ);
      if (ps1::ShouldStartFall(e.state, NextRngByte(), inSector)) {
        SetPhase(s, e, Phase::Fall, "stumble 23");
        HoldSequence(s, e, result);
      }
    }
  }
  e.prevRequest = request;
  return result;
}

// After the update: fall transitions (Step) and the zombie held still while it falls or gets up.
void UpdateFallPhase(void* s, Entry& e) {
  Step(s, e);
  if (e.phase == Phase::Idle) return;
  // The zombie does not move with the enemy logic's step while it falls or gets up.
  uint8_t *o, *t;
  if (GetObjects(s, o, t)) {
    float* accum = game::Field<float>(o, game::O::kAccum);
    accum[0] = accum[1] = accum[2] = 0.0f;
    // 80103C6C substate 0: with the Q1 cursor of 0x23 between start+2 and start+7 the velocity is
    // forward by E+866 << 3 (at most 0x7F) per frame, 8008286C -> E+87C: the stumble carries the
    // zombie forward.
    if (e.phase == Phase::Fall) {
      if (const uint8_t* q = ExitTrack(o)) {
        const int rel = game::Read<int>(q, game::Q::kCursor) - TrackStartCursor(q);
        if (rel >= 2 && rel <= 7) {
          const int step = (std::min)(ZombieWalkSpeed(s, Measure(o, t).distance) * 8, 0x7F);
          const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
          MoveZombie(o, fw[0] * step, fw[2] * step);
        }
      }
    }
    // Backward fall: PS1 push (5 x 20, 18, ..., 2).
    if (e.phase == Phase::BackFall) {
      // 80102D74 substate 3: 08 starts again with P+14 = 0x14 (same step as 0x20).
      const float push = ps1::NextBackPush(e.push) * g_config.backFallPush;
      const float* fw = reinterpret_cast<const float*>(o + game::O::kForward);
      if (push > 0.0f) MoveZombie(o, -fw[0] * push, -fw[2] * push);
    }
  }
}

}  // namespace sf::features::zombie
