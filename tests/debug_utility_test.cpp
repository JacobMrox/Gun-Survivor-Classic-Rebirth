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
  assert(UtilityAmmoCount(false, true, 0) == 0);
  assert(UtilityAmmoCount(true, true, 0) == 1);
  assert(UtilityAmmoCount(true, true, 7) == 7);
  assert(UtilityAmmoCount(true, false, 0) == 0);
  assert(UtilitySuppressAmmoUse(true, true));
  assert(!UtilitySuppressAmmoUse(false, true));
  assert(!UtilitySuppressAmmoUse(true, false));
  assert(UtilitySuppressDamage(true, true, 5));
  assert(!UtilitySuppressDamage(true, true, -5));
  assert(!UtilitySuppressDamage(false, true, 5));
  assert(!UtilitySuppressDamage(true, false, 5));
  assert(UtilityRestoredHealth(true, 50) == 200);
  assert(UtilityRestoredHealth(true, 200) == 200);
  assert(UtilityRestoredHealth(true, 250) == 250);
  assert(UtilityRestoredHealth(true, 0) == 0);
  assert(UtilityRestoredHealth(false, 50) == 50);
  for (int item = 1; item <= 6; ++item) {
    assert(UtilityInventoryAmount(true, item, 23) == -1);
    assert(UtilityInventoryAmount(false, item, 23) == 23);
  }
  assert(UtilityInventoryAmount(true, 9, 5) == -1);
  assert(UtilityInventoryAmount(false, 9, 5) == 5);
  assert(UtilityInventoryAmount(true, 8, -1) == -1);
  assert(UtilityInventoryAmount(false, 8, -1) == -1);
  assert(UtilityInventoryAmount(true, 10, 7) == 7);
  assert(UtilityInventoryAmount(true, 0, 7) == 7);
}
