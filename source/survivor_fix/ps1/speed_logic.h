// PS1 logic of the zombie walk speed.
//
// Source: PS1 zombie code bank at 80100000 (ENEMY_SLOT40), read in Ghidra on 2026-10-15.
//   - init 801047B4: rand & 0xFF against the cumulative table 80105ADC (* 0x33 / 0x14),
//     base from 80105ABC (units per frame, negative in memory: the sign is the direction).
//   - every frame 80105180 -> 80104D5C: E+866 = base, scaled by the distance from the
//     player E+868 (8007F4F4), with a limit of 127.
//   - 8007F210/8007F404: the zombie moves |E+866| units per frame.
//   - rate: VSync(2) in the kernel loop (800110B4, gp+0x100 = 80062E1C = 2 in game),
//     i.e. 30 frames/s.
#pragma once

#include <cstdint>
#include <unordered_map>

namespace ps1 {

constexpr int kWalkSpeeds[8] = {13, 15, 17, 18, 19, 21, 23, 25};      // 80105ABC
constexpr int kWalkSpeedOdds[7] = {8, 28, 42, 56, 70, 82, 92};       // 80105ADC, %
constexpr int kMaxWalkSpeed = 127;                                   // 80104D5C

// 801047B4: zombie base (units per frame) from a random byte.
int PickWalkSpeed(int rngByte);

// 80104D5C: speed of the frame from the base and the distance from the player.
int WalkSpeedForDistance(int base, int distance);

// TEST.BIN 8008286C,80082904..800829C0: final Q8 vector conversion.
// Nearest integer, ties towards +infinity (including negative components).
// Input/output axes must be PS1 axes; reflect PC Z only after rounding.
// Reject non-finite/out-of-range components without changing output.
// This ports the final rounding only, not the SDK/GTE rotation matrix.
bool RoundWalkComponent(float component, int32_t& output);

// 80104E78: animation speed of the walks (clips 1-5 and 0x26-0x29), as multiples of the
// normal speed. Once the blend is over (E+85C == 0) it sets on every track
// 801006E4(E, (n+1) * 0x10), with n = ceil(|P+4 - 80105ABC[0]| / 4): 0x10 is 1x, because
// 8007E38C removes +0x0D from the key frame counter (duration * 0x10). It depends on the
// base, not on the speed scaled by distance.
int WalkAnimMultiplier(int base);

// 801047B4: one base per native lifetime, independent of animation or wall clock.
// The PC adapter supplies the native initialization event, even when an address is reused.
class WalkSpeedLifetime {
 public:
  void Initialize(std::uintptr_t actor, int rngByte);
  void Forget(std::uintptr_t actor);
  int Base(std::uintptr_t actor) const;  // 0 only when this life was not observed yet
 private:
  std::unordered_map<std::uintptr_t, int> bases_;
};

}  // namespace ps1
