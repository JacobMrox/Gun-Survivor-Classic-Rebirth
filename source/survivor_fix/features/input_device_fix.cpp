// InputDeviceFix: start-up fix of the PC game (no PS1 behaviour involved). Bytes and details in
// game.h (kInputDeviceCount): DirectInput devices of an unknown type (virtual keyboards, LEDs,
// headset controls) hung or crashed the game at start-up; they are now skipped.
#include <windows.h>

#include <cstring>

#include "../game.h"
#include "features.h"

namespace sf::features {

void ApplyInputDeviceFix() {
  const game::ByteSignature check = {game::kInputDeviceCount, game::kInputDeviceCountBytes,
                                     sizeof(game::kInputDeviceCountBytes)};
  if (!game::CheckBytes(check)) return;
  void* target = reinterpret_cast<void*>(game::kInputDeviceCount);
  const SIZE_T size = sizeof(game::kInputDeviceCountFix);
  DWORD old = 0;
  if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old)) return;
  std::memcpy(target, game::kInputDeviceCountFix, size);
  VirtualProtect(target, size, old, &old);
  FlushInstructionCache(GetCurrentProcess(), target, size);
}

}  // namespace sf::features
