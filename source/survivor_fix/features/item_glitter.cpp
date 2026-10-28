// Item glitter cadence: the pickup shine uses RESOURCE\\EF04_0.BMP at
// 0045BB25. The PC advances its effect frame by 0.1 on every rendered frame;
// at the wrapper's stable 60 Hz. Slow the full cycle enough to restore the
// PS1-like visible flash followed by a clearly readable dark interval.
#include <windows.h>
#include <cstring>

#include "../game.h"
#include "../log.h"
#include "features.h"

namespace sf::features {

bool ApplyItemGlitterCadence() {
  constexpr uintptr_t kStepA = 0x0045BB10;
  constexpr uintptr_t kStepB = 0x0045BB1B;
  const uint8_t expected[] = {0x68, 0xCD, 0xCC, 0xCC, 0x3D};
  if (!game::CheckBytes({kStepA, expected, sizeof expected}) ||
      !game::CheckBytes({kStepB, expected, sizeof expected})) {
    Log("ItemGlitter: unexpected bytes; native speed kept");
    return false;
  }
  // 0.025f: one quarter of the PC cadence. 0.05 still repeated the bright
  // cells too quickly in a live 60 Hz run.
  const uint8_t slower[] = {0x68, 0xCD, 0xCC, 0xCC, 0x3C};
  for (const uintptr_t address : {kStepA, kStepB}) {
    DWORD old = 0;
    void* target = reinterpret_cast<void*>(address);
    if (!VirtualProtect(target, sizeof slower, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(target, slower, sizeof slower);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof slower);
    DWORD ignored = 0;
    VirtualProtect(target, sizeof slower, old, &ignored);
  }
  Log("ItemGlitter: EF04_0 cadence 0.1 -> 0.025 (quarter speed)");
  return true;
}

}  // namespace sf::features
