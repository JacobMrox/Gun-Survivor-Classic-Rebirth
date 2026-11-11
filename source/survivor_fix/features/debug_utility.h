#pragma once
#include <cmath>

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
// Debug spawn: the room tables of 00430F90 place their actors through one helper at a fixed
// position; the menu uses the player's own forward instead, so the actor appears where the player
// is looking. Pure geometry, so the distance and the degenerate cases are unit-tested.
struct SpawnPoint {
  float x = 0.0f, y = 0.0f, z = 0.0f;
};
constexpr float kDebugSpawnDistance = 400.0f;
// Types verified in the room tables: 0..10 are the zombie models (ENEM00..ENEM10), 11 is Cerberus
// (the type-11 branch of 0043A8A0) and 12 the crow (ps1::crow::kCrowType).
constexpr int kDebugSpawnZombie = 1;
constexpr int kDebugSpawnZombieAlt = 5;
constexpr int kDebugSpawnCerberus = 11;
constexpr int kDebugSpawnCrow = 12;
// The utility menu has one spawn row: LEFT/RIGHT scrolls this table and confirm places the entry,
// so the menu keeps one line instead of one line per actor (the tables and the tests share it).
struct SpawnChoice {
  const char* name;
  int type;   // actor type for 00434850; ignored when item
  bool item;
};
inline constexpr SpawnChoice kDebugSpawnChoices[] = {
    {"ZOMBIE", kDebugSpawnZombie, false},
    {"DOG", kDebugSpawnCerberus, false},
    {"CROW", kDebugSpawnCrow, false},
    {"ITEM", -1, true},
};
inline constexpr int kDebugSpawnChoiceCount =
    static_cast<int>(sizeof(kDebugSpawnChoices) / sizeof(kDebugSpawnChoices[0]));
constexpr int DebugSpawnChoiceWrap(int index) {
  if (index < 0) return kDebugSpawnChoiceCount - 1;
  return index >= kDebugSpawnChoiceCount ? 0 : index;
}
// The spawn point keeps the bandwidth's own height: native placements are entities whose origin
// sits at the body sphere of the player, which is what the room tables pass as y.
inline bool DebugSpawnPoint(const float position[3], const float forward[2], float distance,
                            SpawnPoint* out) {
  if (!position || !forward || !out) return false;
  const float length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
  if (!(length > 1e-4f)) return false;  // no horizontal forward (looking straight up or down)
  const float step = distance / length;
  out->x = position[0] + forward[0] * step;
  out->y = position[1];
  out->z = position[2] + forward[1] * step;
  return std::isfinite(out->x) && std::isfinite(out->y) && std::isfinite(out->z);
}
}
