#include "blood_logic.h"

#include <cstdint>

namespace ps1 {

namespace {

struct Frame {
  int cell;
  int duration;
};

// 800A7410 / 800A744C (cell index, duration) with the cells of 800A7470 (side w = h).
constexpr Frame kA[] = {{8, 2},  {8, 2},  {16, 2}, {24, 2}, {24, 2}, {24, 2}, {32, 2},
                        {32, 2}, {40, 2}, {40, 2}, {40, 2}, {40, 2}, {40, 2}, {40, 3}};
constexpr Frame kB[] = {{8, 2}, {8, 2}, {16, 2}, {24, 2}, {32, 2}, {32, 2}, {32, 2}, {32, 3}};

template <int N>
BloodFrameInfo At(const Frame (&frames)[N], int age) {
  if (age < 0) age = 0;
  for (int i = 0; i < N; ++i) {
    if (age < frames[i].duration) return {i, frames[i].cell};
    age -= frames[i].duration;
  }
  return {-1, frames[N - 1].cell};
}

template <int N>
int Total(const Frame (&frames)[N]) {
  int t = 0;
  for (const Frame& f : frames) t += f.duration;
  return t;
}

int FrameCell(BloodSequence seq, int frame) { return seq == BloodSequence::A ? kA[frame].cell : kB[frame].cell; }

}  // namespace

int BloodDuration(BloodSequence seq) { return seq == BloodSequence::A ? Total(kA) : Total(kB); }

int BloodFrameCount(BloodSequence seq) {
  return seq == BloodSequence::A ? static_cast<int>(sizeof kA / sizeof kA[0])
                                 : static_cast<int>(sizeof kB / sizeof kB[0]);
}

BloodFrameInfo BloodFrameAt(BloodSequence seq, int age) {
  return seq == BloodSequence::A ? At(kA, age) : At(kB, age);
}

int BloodPcCell(BloodSequence seq, int age, int cells) {
  const BloodFrameInfo now = BloodFrameAt(seq, age);
  if (now.frame < 0) return -1;
  const int count = BloodFrameCount(seq);
  const int full = BloodFrameAt(seq, BloodDuration(seq) - 1).cell;
  int first = count - 1;
  while (first > 0 && FrameCell(seq, first - 1) == full) --first;
  if (now.cell != full) return 0;
  const int span = count - 1 - first;
  return span > 0 ? ((now.frame - first) * (cells - 1) * 2 + span) / (span * 2) : cells - 1;
}

int BloodScaledSide(int cell) { return (cell * 0xAA) >> 8; }

int BloodDepth(int distance) {
  const uint32_t five = static_cast<uint32_t>(distance) * 5u;
  const int third = static_cast<int>((static_cast<uint64_t>(five) * 0xAAAAAAABull) >> 32);
  return static_cast<int16_t>(static_cast<uint32_t>(third << 14) >> 16);
}

}  // namespace ps1
