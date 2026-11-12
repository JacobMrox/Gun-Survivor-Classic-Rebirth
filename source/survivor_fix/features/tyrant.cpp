// Mr. X (PC types 22/23): the PS1 walk/idle clip choice and the PS1 death clip, from the travel the
// native physics actually produced. Nothing here moves the actor: the enemy logic, collision, attacks
// and damage stay native (ps1/tyrant_logic.h has the PS1 references).
#include <cmath>
#include <unordered_map>

#include "../config.h"
#include "../game.h"
#include "../ps1/tyrant_logic.h"
#include "features.h"

namespace sf::features::tyrant {
namespace {

struct Track {
  const uint8_t* o = nullptr;
  int index = -1;
  float x = 0.0f, z = 0.0f;
  float travel = 0.0f;  // planar units between two updates
  bool standing = false;
  bool sampled = false;
};

std::unordered_map<void*, Track> g_tracks;

bool Objects(void* s, uint8_t** o) {
  auto* collection = game::Read<uint8_t*>(s, game::S::kCollection);
  const int index = game::Read<int>(s, game::S::kIndex);
  if (!collection || index < 0) return false;
  auto** objects = game::Read<uint8_t**>(collection, game::C::kObjects);
  if (!objects || index >= game::Read<int>(collection, game::C::kCount) || !objects[index]) return false;
  *o = objects[index];
  return true;
}

}  // namespace

// Called before the enemy logic of every update. The native physics applies the accumulated movement
// after the update, so the position difference since the previous call is exactly one update of
// travel. S addresses are reused: a different object or slot restarts the sample instead of measuring
// the distance between two lives.
void BeginUpdate(void* s) {
  if (!g_config.ps1Tyrant || !ps1::IsTyrant(game::Read<int>(s, game::S::kType))) {
    g_tracks.erase(s);
    return;
  }
  uint8_t* o = nullptr;
  if (!Objects(s, &o)) {
    g_tracks.erase(s);
    return;
  }
  const int index = game::Read<int>(s, game::S::kIndex);
  const float* pos = reinterpret_cast<const float*>(o + game::O::kPos);
  Track& t = g_tracks[s];
  if (t.o != o || t.index != index) {
    t = Track();
    t.o = o;
    t.index = index;
  }
  if (t.sampled) {
    const float dx = pos[0] - t.x;
    const float dz = pos[2] - t.z;
    t.travel = std::sqrt(dx * dx + dz * dz);
  }
  t.x = pos[0];
  t.z = pos[2];
  t.sampled = true;
}

// Runs where the native mapper result is final. Only the death clip and the walk clip change; every
// other request keeps the PC's own selector, threshold and speed.
void Map(void* s, int request, int& result) {
  if (!g_config.ps1Tyrant || !ps1::IsTyrant(game::Read<int>(s, game::S::kType))) return;
  if (ps1::IsTyrantDeathRequest(request)) {
    result = ps1::kTyrantDeathSelector;
    return;
  }
  if (!ps1::IsTyrantWalkRequest(request) || result != ps1::kTyrantWalkSelector) return;
  auto it = g_tracks.find(s);
  if (it == g_tracks.end()) return;
  Track& t = it->second;
  t.standing = ps1::TyrantStanding(t.standing, t.travel);
  *game::Field<float>(s, game::S::kNextSpeed) =
      t.standing ? ps1::kTyrantIdleSpeed : ps1::TyrantWalkClipSpeed(t.travel);
  result = ps1::TyrantWalkSelector(t.standing);
}

void Reset() { g_tracks.clear(); }

}  // namespace sf::features::tyrant
