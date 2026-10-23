#include "pose_logic.h"

namespace ps1 {

int TrackHead(int headYaw, bool playerOnPositiveSide) {
  const int step = playerOnPositiveSide ? -kHeadTurnStep : kHeadTurnStep;
  if ((step < 0 && headYaw > -kHeadTurnLimit) || (step > 0 && headYaw < kHeadTurnLimit)) return headYaw + step;
  return headYaw;
}

int RelaxHead(int headYaw) {
  if (headYaw > 0) return headYaw - kHeadTurnStep > 0 ? headYaw - kHeadTurnStep : 0;
  if (headYaw < 0) return headYaw + kHeadTurnStep < 0 ? headYaw + kHeadTurnStep : 0;
  return 0;
}

int TwistForHit(bool behind) { return behind ? kTwistHit : -kTwistHit; }

int DecayTwist(int twist) {
  if (twist == 0) return 0;
  return twist < 0 ? twist + kTwistDecay : twist - kTwistDecay;
}

}  // namespace ps1
