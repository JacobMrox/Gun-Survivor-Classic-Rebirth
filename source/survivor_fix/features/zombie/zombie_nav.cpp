// Ps1ZombieNavigation: direction and step of the zombie decided by the PS1 logic of state 4
// (ps1/nav_logic.*), both with a clear line (8007F210) and with a wall in between (80100E24).
// The PC keeps only the forward movement at its own speed (S+74).
//   - The logic works in PS1 space, where z = -z of the PC (same x; measured in room 7/0):
//     positions and directions are converted on the way in and out.
//   - The direction is written into O's matrix (game.h, O::kMatrix) before the PC enemy logic and again
//     after it, to cancel the +/-3 degrees that 0043A8A0 adds by itself.
//   - To make the zombie walk the PC enemy logic aims straight ahead (lateral offset < 200: goes straight,
//     request 2); to make it turn on the spot it aims to the side (turns, requests 3/4, with their
//     animation). T+B8 is restored at once; the attacks use T+8.
// ASSUMPTION: T+B8 is read only for aiming during the zombie's update.
// RateScale 1.0: the PC does ~29.4 updates/s (34 ms limiter) and the PS1 ~30 frames/s.
#include "zombie_shared.h"

namespace sf::features::zombie {

int UpdateWithNavigation(void* s, void* edx) {
  if (!g_config.ps1ZombieNavigation || !ps1::IsPcZombie(game::Read<int>(s, game::S::kType))) {
    return g_updateEnemy(s, edx);
  }
  auto it = g_entries.find(s);
  // A valid empty N1 is open space, not a missing geometry profile. Creation is per native life/reset;
  // no wall-clock lifetime. Native XZ is never frozen without letting the common navigation path
  // produce its corresponding plan.
  if (it == g_entries.end() && WorldEnabled()) it = g_entries.try_emplace(s).first;
  const auto& walls = CurrentRoomWalls();
  if (it == g_entries.end() || it->second.phase != Phase::Idle || (walls.empty() && !WorldEnabled()) ||
      !IsLocomotion(game::Read<int>(s, game::S::kRequest))) {
    return g_updateEnemy(s, edx);
  }
  uint8_t *o, *t;
  if (!GetObjects(s, o, t)) return g_updateEnemy(s, edx);
  Entry& e = it->second;
  const float* op = reinterpret_cast<const float*>(o + game::O::kPos);
  const float* of = reinterpret_cast<const float*>(o + game::O::kForward);
  const float* tp = reinterpret_cast<const float*>(t + game::O::kPos);
  ps1::NavParams params;
  params.probeDistance = g_config.navProbeDistance;
  params.turnStep = g_config.navTurnStep * 3.14159265f / 180.0f;
  params.rateScale = g_config.navRateScale;
  params.playerWallMargin = g_config.navPlayerWallMargin;
  if (WorldEnabled()) {
    // 8007F210 in integers with the frame's E+866 (negative on the PS1, 80104D5C): no +/-6 unit wobble
    // at every update when the zombie is aligned.
    params.ps1IntegerSteer = true;
    params.ps1Speed = -ZombieWalkSpeed(s, std::hypot(tp[0] - op[0], tp[2] - op[2]));
    params.rateScale = 1.0f;
    params.turnStep = ps1::kTurnStep;
    params.playerWallMargin = 0.0f;  // the same ROOMENV walls now own the player/world step
  }
  const ps1::NavOutput nav = ps1::NavStep(e.nav, walls, {{op[0], -op[2]}, {of[0], -of[2]}, {tp[0], -tp[2]}}, params);

  // Direction in PC coordinates and aim point for the PC enemy logic.
  const float hx = nav.heading.x, hz = -nav.heading.z;
  float ax = hx, az = hz;
  if (!nav.walk) {
    // To the side, towards the turn (in PS1 space +90 = left), then in PC space.
    const ps1::Vec2 side = ps1::Rotate(nav.heading, nav.turnLeft ? ps1::kSideProbeAngle : -ps1::kSideProbeAngle);
    ax = side.x;
    az = -side.z;
  }
  WorldPlanWalk(s, of[0], of[2], nav.walk);  // 8007F210 moves using the heading before the rotation.
  SetHeading(o, hx, hz);
  float* aimPoint = game::Field<float>(t, game::O::kAimPoint);
  const float saved[3] = {aimPoint[0], aimPoint[1], aimPoint[2]};
  aimPoint[0] = op[0] + ax * g_config.navAimDistance;
  aimPoint[2] = op[2] + az * g_config.navAimDistance;
  const int result = g_updateEnemy(s, edx);
  aimPoint[0] = saved[0];
  aimPoint[1] = saved[1];
  aimPoint[2] = saved[2];
  SetHeading(o, hx, hz);
  return result;
}

}  // namespace sf::features::zombie
