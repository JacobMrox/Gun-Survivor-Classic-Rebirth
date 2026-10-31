// Item glitter cadence.  EF04_0 is a generic sprite effect created from the
// item constructor at 0045BB25.  The item model itself is drawn by 0045BC70;
// suppressing that routine makes the pickup blink, which is not the PS1 effect.
#include <windows.h>

#include <unordered_set>

#include "../game.h"
#include "../log.h"
#include "features.h"

namespace sf::features {
namespace {

std::unordered_set<void*> g_itemGlitters;

}  // namespace

void RegisterItemGlitterEffect(void* effect) {
  if (effect) g_itemGlitters.insert(effect);
}

bool ItemGlitterShouldDraw(void* effect) {
  if (!effect || g_itemGlitters.find(effect) == g_itemGlitters.end()) return true;
  // Two brief star flashes, then one full second without the sprite.  The
  // native effect remains alive while hidden; only EF04's draw is omitted.
  const DWORD phase = GetTickCount() % 2000;
  return phase < 150 || (phase >= 500 && phase < 650);
}

bool IsItemGlitterEffect(void* effect) {
  return effect && g_itemGlitters.find(effect) != g_itemGlitters.end();
}

void ForgetItemGlitterEffect(void* effect) { g_itemGlitters.erase(effect); }

void ResetItemGlitterEffects() { g_itemGlitters.clear(); }

bool ApplyItemGlitterCadence() {
  const uint8_t call[] = {0xE8, 0xA6, 0x34, 0xFB, 0xFF};
  if (!game::CheckBytes({0x0045BB25, call, sizeof call})) {
    Log("ItemGlitter: EF04 creation call not recognised; native cadence kept");
    return false;
  }
  Log("ItemGlitter: EF04-only cadence armed (two flashes, one-second pause); item mesh unchanged");
  return true;
}

}  // namespace sf::features
