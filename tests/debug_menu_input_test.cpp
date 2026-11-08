#include "../source/survivor_fix/features/debug_menu_input.h"
#include <cassert>

int main() {
  sf::features::DebugMenuInputGate gate;
  assert(!gate.Filter(false, false, 0x20));
  assert(gate.Filter(true, true, 0x20));
  assert(gate.Filter(false, true, 0x20));
  assert(gate.Filter(false, false, 0x20));
  assert(gate.Filter(false, false, 0));
  assert(!gate.Filter(false, false, 0x20));
  assert(gate.Filter(true, false, 0));
  assert(gate.Filter(false, true, 0));
  assert(gate.Filter(false, false, 0));
  assert(!gate.Filter(false, false, 1));
  gate.Capture(); // Menu opened and closed between native pad reads.
  assert(gate.Filter(false, true, 0));
  assert(gate.Filter(false, false, 0));
  assert(!gate.Filter(false, false, 1));
}
