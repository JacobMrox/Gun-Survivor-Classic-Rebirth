// Input state for the native movie completion check. No worker or video API hook.
#pragma once
#include <cstdint>
namespace sf {
class MovieSkipGate {
 public:
  void Begin(uint32_t keys) { previous_ = keys; }
  bool Poll(bool automatic, bool foreground, uint32_t keys, bool completed) {
    const uint32_t pressed = keys & ~previous_;
    previous_ = keys;
    return completed || automatic || (foreground && pressed != 0);
  }
 private:
  uint32_t previous_ = 0;
};
}  // namespace sf
