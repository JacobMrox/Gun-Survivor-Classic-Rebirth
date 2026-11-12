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
// The room spawner 00434850 accepts actor types 0..0x25. The game's nab folder ships one model per
// type (ENEM00..ENEM37); 03, 21, 24, 25, 27 and 29 have no NAB file in this install, so those
// entries carry a star and the menu logs a warning for them. Whether those ids are the human/NPC
// actors that would use an HMD model instead is not verified: the Resource folder only holds the
// DEBUGHxx.HMD debug humans, so they are offered by id rather than guessed at.
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
    {"ENEM00", 0, false},
    {"ZOMBIE", 1, false},
    {"ENEM02", 2, false},
    {"ENEM03*", 3, false},
    {"ENEM04", 4, false},
    {"ENEM05", 5, false},
    {"ENEM06", 6, false},
    {"ENEM07", 7, false},
    {"ENEM08", 8, false},
    {"ENEM09", 9, false},
    {"ENEM10", 10, false},
    {"DOG", 11, false},
    {"CROW", 12, false},
    {"ENEM13", 13, false},
    {"ENEM14", 14, false},
    {"ENEM15", 15, false},
    {"ENEM16", 16, false},
    {"ENEM17", 17, false},
    {"ENEM18", 18, false},
    {"ENEM19", 19, false},
    {"ENEM20", 20, false},
    {"ENEM21*", 21, false},
    {"ENEM22", 22, false},
    {"ENEM23", 23, false},
    {"ENEM24*", 24, false},
    {"ENEM25*", 25, false},
    {"ENEM26", 26, false},
    {"ENEM27*", 27, false},
    {"ENEM28", 28, false},
    {"ENEM29*", 29, false},
    {"ENEM30", 30, false},
    {"ENEM31", 31, false},
    {"ENEM32", 32, false},
    {"ENEM33", 33, false},
    {"ENEM34", 34, false},
    {"ENEM35", 35, false},
    {"ENEM36", 36, false},
    {"ENEM37", 37, false},
    {"ITEM", -1, true},
};

inline constexpr int kDebugSpawnChoiceCount =
    static_cast<int>(sizeof(kDebugSpawnChoices) / sizeof(kDebugSpawnChoices[0]));

// Actor ids whose model file is absent from nab in this install.
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
