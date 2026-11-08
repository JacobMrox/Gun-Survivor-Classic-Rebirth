#include "../source/survivor_fix/features/debug_utility.h"
#include <cassert>

int main() {
  using namespace sf::features;
  assert(UtilityActionForRow(0) == UtilityAction::QuickSave);
  assert(UtilityActionForRow(1) == UtilityAction::QuickLoad);
  assert(UtilityActionForRow(2) == UtilityAction::SaveMenu);
  assert(UtilityActionForRow(3) == UtilityAction::LoadMenu);
  assert(UtilityActionForRow(4) == UtilityAction::Unavailable);
  assert(UtilityActionForRow(5) == UtilityAction::Unavailable);
  assert(UtilityActionForRow(-1) == UtilityAction::Unavailable);
  assert(UtilityActionForRow(6) == UtilityAction::Unavailable);
  assert(!UtilityActionReady(UtilityAction::None, false));
  assert(!UtilityActionReady(UtilityAction::QuickSave, true));
  assert(UtilityActionReady(UtilityAction::QuickSave, false));
}
