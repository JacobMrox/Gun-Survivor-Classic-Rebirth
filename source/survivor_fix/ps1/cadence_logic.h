// PS1 frame rate: two NTSC vertical blanks per game frame.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ps1 {

// Kernel 800255F8/80025770: wait(last+1), then wait(current+1), remember current.
// NTSC GPU 53693175 Hz, 3413 clocks/line, 263 lines: two VBlanks ~= 29.908667 Hz.
// Hardware constants, not a measured sample or a speed multiplier.
class NtscVBlankCadence {
 public:
  void Reset() {
    origin_ = frequency_ = lastTime_ = 0;
    lastBlank_ = target_ = 0;
    ready_ = false;
    phase_ = 0;
  }

  int64_t Begin(int64_t now, int64_t frequency) {
    if (now < 0 || frequency < 1000 || frequency > 1000000000) return 0;
    if (!ready_) {
      origin_ = lastTime_ = now;
      frequency_ = frequency;
      ready_ = true;
    }
    if (frequency != frequency_ || now < lastTime_) {
      Reset();
      return 0;
    }
    const auto current = Current(now);
    if (current == UINT64_MAX || lastBlank_ >= UINT64_MAX - 2) return 0;
    target_ = lastBlank_ + 1;
    phase_ = 1;
    const auto deadline = At(target_);
    return deadline ? (std::max)(now, deadline) : 0;
  }

  int64_t SecondDeadline(int64_t now) {
    if (!ready_ || phase_ != 1 || now < lastTime_) return 0;
    const auto current = Current(now);
    if (current == UINT64_MAX || current < target_ || current >= UINT64_MAX - 1) return 0;
    target_ = current + 1;
    phase_ = 2;
    return At(target_);
  }

  bool Finish(int64_t now) {
    if (!ready_ || phase_ != 2 || now < lastTime_ || !target_) return false;
    const auto current = Current(now);
    if (current == UINT64_MAX || current < target_) return false;
    lastBlank_ = current;
    lastTime_ = now;
    phase_ = 0;
    return true;
  }

 private:
  int64_t At(uint64_t blank) const {
    constexpr uint64_t den = 53693175;
    const uint64_t num = uint64_t(frequency_) * 897619;
    const auto q = num / den, rem = num % den;
    const auto limit = uint64_t((std::numeric_limits<int64_t>::max)() - origin_);
    if ((q && blank > limit / q) || (rem && blank > (UINT64_MAX - den + 1) / rem)) return 0;
    const auto whole = blank * q, part = (blank * rem + den - 1) / den;
    if (part > limit - whole) return 0;
    return origin_ + int64_t(whole + part);
  }

  uint64_t Current(int64_t now) const {
    if (now < origin_) return UINT64_MAX;
    const auto elapsed = now - origin_;
    const double estimate = double(elapsed) * 53693175.0 / (double(frequency_) * 897619.0);
    if (!std::isfinite(estimate) || estimate >= double(UINT64_MAX - 2)) return UINT64_MAX;
    auto n = uint64_t(estimate);
    // Float only seeds the search; exact rational deadlines decide the boundary.
    auto at = At(n);
    if (!at && n) return UINT64_MAX;
    while (n && at > now) at = At(--n);
    while (true) {
      const auto next = At(n + 1);
      if (!next) return UINT64_MAX;
      if (next > now) break;
      ++n;
    }
    return n;
  }

  int64_t origin_ = 0, frequency_ = 0, lastTime_ = 0;
  uint64_t lastBlank_ = 0, target_ = 0;
  bool ready_ = false;
  int phase_ = 0;
};

}  // namespace ps1
