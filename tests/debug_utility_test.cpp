#include "../source/survivor_fix/features/debug_utility.h"
#include "../source/survivor_fix/ps1/free_cam_logic.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
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
  // A debug spawn follows the free camera's bearing while it is on: the player's body forward lags it.
  {
    const float player[2] = {0.0f, -1.0f};
    const float cam[3] = {-0.8f, 0.1f, 0.6f};
    float facing[2] = {0.0f, 0.0f};
    DebugSpawnForward(player, cam, true, facing);
    assert(facing[0] == -0.8f && facing[1] == 0.6f);
    DebugSpawnForward(player, cam, false, facing);
    assert(facing[0] == 0.0f && facing[1] == -1.0f);
    const float down[3] = {0.0f, 1.0f, 0.0f};  // straight down: no horizontal bearing to follow
    DebugSpawnForward(player, down, true, facing);
    assert(facing[0] == 0.0f && facing[1] == -1.0f);
  }
  assert(kDebugSpawnZombie >= 0 && kDebugSpawnZombieAlt >= 0);
  assert(kDebugSpawnCerberus == 11 && kDebugSpawnCrow == 12);
  // The utility menu has one spawn row: LEFT/RIGHT scrolls this table.
  // One entry per actor type 0..0x25, in id order, with the item last: the SPAWN row scrolls the
  // same table the spawner uses, so a type is never silently unreachable.
  // One entry per actor type 0..0x25, in id order, with the item last: the SPAWN row scrolls the
  // same table the spawner uses, so a type is never silently unreachable.
  assert(kDebugSpawnChoiceCount == kDebugSpawnMaxType + 2);
  for (int type = 0; type <= kDebugSpawnMaxType; ++type) {
    const SpawnChoice& choice = kDebugSpawnChoices[type];
    assert(choice.type == type);
    assert(!choice.item);
    // Every label reads "ENEMxx - Name", so the row and the model file agree at a glance.
    assert(std::strncmp(choice.name, "ENEM", 4) == 0);
    assert(std::atoi(choice.name + 4) == type);
    assert(std::strstr(choice.name, " - ") != nullptr);
    // "%c SPAWN < %s >" has to fit the menu's 48-byte line buffer.
    assert(std::strlen(choice.name) <= 35);
  }
  assert(kDebugSpawnChoices[kDebugSpawnMaxType + 1].item);
  assert(kDebugSpawnChoices[kDebugSpawnMaxType + 1].type < 0);
  assert(std::strcmp(kDebugSpawnChoices[11].name, "ENEM11 - Zombie Dog") == 0);
  assert(std::strstr(kDebugSpawnChoices[13].name, "Licker") != nullptr);
  assert(std::strstr(kDebugSpawnChoices[18].name, "Crocodile") != nullptr);
  assert(std::strstr(kDebugSpawnChoices[22].name, "Mr. X") != nullptr);
  assert(std::strstr(kDebugSpawnChoices[28].name, "Hunter") != nullptr);
  assert(std::strstr(kDebugSpawnChoices[30].name, "Umbrella Cleaner") != nullptr);
  assert(std::strstr(kDebugSpawnChoices[36].name, "Giant Grey Spider") != nullptr);
  // Model files: 07 was added by hand and counts as present; these ids have none in this install.
  assert(DebugSpawnTypeHasModel(7));
  assert(!DebugSpawnTypeHasModel(3));
  assert(!DebugSpawnTypeHasModel(21));
  assert(!DebugSpawnTypeHasModel(24));
  assert(!DebugSpawnTypeHasModel(25));
  assert(!DebugSpawnTypeHasModel(27));
  assert(!DebugSpawnTypeHasModel(29));
  assert(DebugSpawnTypeHasModel(0) && DebugSpawnTypeHasModel(37));
  assert(std::strstr(kDebugSpawnChoices[7].name, "WIP") != nullptr);
  // 03 has no files either, and 37 is a straight duplicate of 36.
  assert(std::strstr(kDebugSpawnChoices[3].name, "Missing") != nullptr);
  assert(std::strstr(kDebugSpawnChoices[37].name, "Giant Grey Spider") != nullptr);
  assert(kDebugSpawnZombieWip == 7);
  assert(!DebugSpawnTypeHasModel(kDebugSpawnZombieWip) == false);  // 07 has hand-added files
  assert(DebugSpawnChoiceWrap(-1) == kDebugSpawnChoiceCount - 1);
  assert(DebugSpawnChoiceWrap(kDebugSpawnChoiceCount) == 0);
  assert(DebugSpawnChoiceWrap(2) == 2);
  // Spawn distance setting: the default keeps the old 400, and the range is clamped.
  assert(kDebugSpawnDistance == 400.0f);
  assert(DebugSpawnDistanceClamped(kDebugSpawnDistance) == 400.0f);
  assert(DebugSpawnDistanceClamped(650.0f) == 650.0f);
  assert(DebugSpawnDistanceClamped(0.0f) == kDebugSpawnDistanceMin);
  assert(DebugSpawnDistanceClamped(-50.0f) == kDebugSpawnDistanceMin);
  assert(DebugSpawnDistanceClamped(9000.0f) == kDebugSpawnDistanceMax);
  assert(DebugSpawnDistanceClamped(std::nanf("")) == kDebugSpawnDistance);
  assert(DebugSpawnDistanceClamped(99.9f) == kDebugSpawnDistanceMin);
  // Free camera: normalised direction, level strafe basis, yaw around Y and the step combination.
  {
    float dir[3] = {0.0f, 0.0f, -4.0f};
    assert(ps1::FreeCamNormalize(dir, dir));
    assert(std::fabs(dir[2] + 1.0f) < 1e-5f);
    float right[3];
    ps1::FreeCamRight(dir, right);
    assert(std::fabs(right[0] - 1.0f) < 1e-5f && right[1] == 0.0f && std::fabs(right[2]) < 1e-5f);
    float pos[3] = {0.0f, 0.0f, 0.0f};
    ps1::FreeCamAdvance(pos, dir, right, 1.0f, 0.0f, 0.0f, 10.0f);
    assert(std::fabs(pos[2] + 10.0f) < 1e-4f);
    ps1::FreeCamAdvance(pos, dir, right, 0.0f, 1.0f, 1.0f, 10.0f);
    assert(std::fabs(pos[0] - 10.0f) < 1e-4f && std::fabs(pos[1] - 10.0f) < 1e-4f);
    // Looking down -Z and turning right by 90 degrees ends up looking down +X, the strafe side.
    ps1::FreeCamYaw(dir, 1.57079633f);
    assert(std::fabs(dir[0] - 1.0f) < 1e-4f && std::fabs(dir[2]) < 1e-4f);
    const float flat[3] = {0.0f, 0.0f, 0.0f};
    float out[3] = {1.0f, 1.0f, 1.0f};
    assert(!ps1::FreeCamNormalize(flat, out));
  }
}
