#pragma once
namespace sf::features {
enum class UtilityAction { None, QuickSave, QuickLoad, SaveMenu, LoadMenu, Unavailable };
constexpr UtilityAction UtilityActionForRow(int row) {
  switch (row) {
    case 0: return UtilityAction::QuickSave;
    case 1: return UtilityAction::QuickLoad;
    case 2: return UtilityAction::SaveMenu;
    case 3: return UtilityAction::LoadMenu;
    default: return UtilityAction::Unavailable;
  }
}
constexpr bool UtilityActionReady(UtilityAction action, bool inputCaptured) {
  return action != UtilityAction::None && action != UtilityAction::Unavailable && !inputCaptured;
}
constexpr int UtilityAmmoCount(bool enabled, bool playerStats, int count) {
  return enabled && playerStats && count == 0 ? 1 : count;
}
constexpr bool UtilitySuppressAmmoUse(bool enabled, bool playerStats) {
  return enabled && playerStats;
}
constexpr bool UtilitySuppressDamage(bool enabled, bool playerStats, int damage) {
  return enabled && playerStats && damage > 0;
}
// PC stats constructor 00463D1B initializes player HP to 0xC8.
constexpr int UtilityRestoredHealth(bool enabled, int health) {
  return enabled && health > 0 && health < 200 ? 200 : health;
}
// Inventory builder 00589B00 uses item IDs 1..6 and 9 for weapons;
// the native item renderer uses -1 to draw its infinity glyph.
constexpr int UtilityInventoryAmount(bool enabled, int itemId, int amount) {
  return enabled && ((itemId >= 1 && itemId <= 6) || itemId == 9) ? -1 : amount;
}
}
