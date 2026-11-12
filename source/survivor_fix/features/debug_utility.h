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
// The room spawner 00434850 accepts actor types 0..0x25, and the game's nab folder ships one model
// per type (ENEM00..ENEM37). The names are taken from those models: 00-10 are the zombie variants, 11
// the Cerberus, 12 the crow, 13/14 the lickers, 15 the cockroach, 16/17/34/35/36 the spiders, 18 the
// crocodile, 19/20 the ivy, 22-24 Mr. X, 26 the moth, 28 the hunter, 30 the Umbrella cleaner and 31-33
// the Hypnos-T forms. 07 is the extra zombie slot with a WIP NAB/NAC added by hand; 03, 21, 24, 25, 27
// and 29 have no model file in this install and carry a "Missing" label, and 37 has no name yet.
// Every label reads "ENEMxx - Name" and stays inside the menu's 48-byte line.
constexpr int kDebugSpawnZombie = 1;
constexpr int kDebugSpawnZombieAlt = 5;
constexpr int kDebugSpawnCerberus = 11;
constexpr int kDebugSpawnCrow = 12;
constexpr int kDebugSpawnMaxType = 0x25;

struct SpawnChoice {
  const char* name;
  int type;   // actor type for 00434850; ignored when item
  bool item;
};

inline constexpr SpawnChoice kDebugSpawnChoices[] = {
    {"ENEM00 - Zombie", 0, false},
    {"ENEM01 - Zombie", 1, false},
    {"ENEM02 - Zombie", 2, false},
    {"ENEM03 - Zombie", 3, false},
    {"ENEM04 - Zombie", 4, false},
    {"ENEM05 - Zombie", 5, false},
    {"ENEM06 - Zombie", 6, false},
    {"ENEM07 - Zombie (WIP)", 7, false},
    {"ENEM08 - Zombie", 8, false},
    {"ENEM09 - Zombie", 9, false},
    {"ENEM10 - Zombie", 10, false},
    {"ENEM11 - Zombie Dog", 11, false},
    {"ENEM12 - Zombie Crow", 12, false},
    {"ENEM13 - Licker", 13, false},
    {"ENEM14 - Super Licker", 14, false},
    {"ENEM15 - Cockroach", 15, false},
    {"ENEM16 - Big Spider", 16, false},
    {"ENEM17 - Giant Grey Spider", 17, false},
    {"ENEM18 - Crocodile", 18, false},
    {"ENEM19 - Ivy 1", 19, false},
    {"ENEM20 - Ivy 2", 20, false},
    {"ENEM21 - Missing", 21, false},
    {"ENEM22 - Mr. X", 22, false},
    {"ENEM23 - Mr. X", 23, false},
    {"ENEM24 - Mr. X", 24, false},
    {"ENEM25 - Missing", 25, false},
    {"ENEM26 - Moth", 26, false},
    {"ENEM27 - Missing", 27, false},
    {"ENEM28 - Hunter", 28, false},
    {"ENEM29 - Missing", 29, false},
    {"ENEM30 - Umbrella Cleaner", 30, false},
    {"ENEM31 - Hypno-T Form 1", 31, false},
    {"ENEM32 - Hypno-T Form 2", 32, false},
    {"ENEM33 - Hypno-T Form 3", 33, false},
    {"ENEM34 - Big Spider", 34, false},
    {"ENEM35 - Big Spider", 35, false},
    {"ENEM36 - Giant Grey Spider", 36, false},
    {"ENEM37 - Unknown", 37, false},
    {"ITEM", -1, true},
};

inline constexpr int kDebugSpawnChoiceCount =
    static_cast<int>(sizeof(kDebugSpawnChoices) / sizeof(kDebugSpawnChoices[0]));

// Actor ids with no model file in this install. 07 is not here: its NAB/NAC were added by hand.
inline constexpr int kDebugSpawnNoModelTypes[] = {3, 21, 24, 25, 27, 29};

constexpr bool DebugSpawnTypeHasModel(int type) {
  for (int missing : kDebugSpawnNoModelTypes) {
    if (missing == type) return false;
  }
  return true;
}
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
