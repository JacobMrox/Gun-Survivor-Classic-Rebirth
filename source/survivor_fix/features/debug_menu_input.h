#pragma once
#include <cstdint>

namespace sf::features {
// Suppress the release sample too, so a menu-closing key cannot reach gameplay.
class DebugMenuInputGate {
 public:
  void Capture() { captured_ = true; }
  bool Filter(bool menuOpen, bool menuKeyDown, uint32_t pad) {
    if (menuOpen) captured_ = true;
    if (!captured_) return false;
    if (!menuOpen && !menuKeyDown && pad == 0) captured_ = false;
    return true;
  }
 private:
  bool captured_ = false;
};
}
