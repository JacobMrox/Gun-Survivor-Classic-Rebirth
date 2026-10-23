// Drain native mouse batches without changing the event data or reading another device.
#pragma once
namespace sf {
struct MouseBatch { int result; unsigned motionEvents; };
struct MouseDrainResult { int result; unsigned reads; };
template<class Read> MouseDrainResult DrainMouse(Read read, unsigned limit = 64) {
  const MouseBatch first = read();
  unsigned reads = 1;
  MouseBatch last = first;
  while (last.result && last.motionEvents && reads < limit) {
    last = read();
    ++reads;
  }
  return {first.result, reads};
}
}  // namespace sf
