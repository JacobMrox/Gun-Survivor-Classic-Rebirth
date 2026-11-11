#include "../source/survivor_fix/features/debug_utility.h"
#include <cassert>
#include <cmath>
#include <cstring>

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
  // Debug spawn: one step in front of the player, on the player's own floor.
  const float at[3] = {100.0f, -1400.0f, 50.0f};
  const float forward[2] = {0.0f, -1.0f};
  SpawnPoint point;
  assert(DebugSpawnPoint(at, forward, kDebugSpawnDistance, &point));
  assert(point.x == 100.0f);
  assert(point.y == -1400.0f);
  assert(point.z == 50.0f - kDebugSpawnDistance);
  const float diagonal[2] = {3.0f, 4.0f};  // not unit length: the distance must not change
  assert(DebugSpawnPoint(at, diagonal, kDebugSpawnDistance, &point));
  assert(std::fabs(point.x - (100.0f + kDebugSpawnDistance * 0.6f)) < 0.01f);
  assert(std::fabs(point.y - -1400.0f) < 0.01f);
  assert(std::fabs(point.z - (50.0f + kDebugSpawnDistance * 0.8f)) < 0.01f);
  const float zero[2] = {0.0f, 0.0f};
  assert(!DebugSpawnPoint(at, zero, kDebugSpawnDistance, &point));
  assert(!DebugSpawnPoint(nullptr, forward, kDebugSpawnDistance, &point));
  assert(!DebugSpawnPoint(at, forward, kDebugSpawnDistance, nullptr));
  assert(DebugSpawnPoint(at, forward, 0.0f, &point) && point.x == 100.0f);
  assert(kDebugSpawnZombie >= 0 && kDebugSpawnZombieAlt >= 0);
  assert(kDebugSpawnCerberus == 11 && kDebugSpawnCrow == 12);
  // The utility menu has one spawn row: LEFT/RIGHT scrolls this table.
  assert(kDebugSpawnChoiceCount == 4);
  assert(std::strcmp(kDebugSpawnChoices[0].name, "ZOMBIE") == 0);
  assert(kDebugSpawnChoices[0].type == kDebugSpawnZombie && !kDebugSpawnChoices[0].item);
  assert(kDebugSpawnChoices[1].type == kDebugSpawnCerberus);
  assert(kDebugSpawnChoices[2].type == kDebugSpawnCrow);
  assert(kDebugSpawnChoices[3].item && kDebugSpawnChoices[3].type < 0);
  assert(DebugSpawnChoiceWrap(-1) == kDebugSpawnChoiceCount - 1);
  assert(DebugSpawnChoiceWrap(kDebugSpawnChoiceCount) == 0);
  assert(DebugSpawnChoiceWrap(2) == 2);
}
