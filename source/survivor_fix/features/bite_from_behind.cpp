// BiteFromBehind: zombies (and the other monsters) also bite from behind.
//
// In 0043A8A0 (enemy logic update, shared by all types) the attack event 0043BC70 starts only if, besides
// the distance, the player->monster direction lies in the half plane in front of the player:
// 0045E870 with ECX = object 0 (the player) gives its local (0,0,-1), i.e. the front (checked in
// game), and if the dot product is < 0 the jump to 0043AB69 (JNZ +7) skips CALL 0043BC70. So the
// player is never hit from behind. On the PS1 the frontal attack also needs the monster in front,
// but from behind there is the grab from behind (80101948, state 0x0E): the player is hit anyway.
// Here the JNZ becomes two NOPs: the distance check stays.
// TODO: port the PS1 grab from behind instead of this generic bite.
#include <windows.h>

#include <cstring>

#include "../game.h"
#include "../log.h"
#include "features.h"

namespace sf::features {

bool InstallBiteFromBehind() {
  const game::ByteSignature check = {game::kBiteFacingCheck, game::kBiteFacingCheckBytes,
                                     sizeof(game::kBiteFacingCheckBytes)};
  if (!game::CheckBytes(check)) {
    Log("BiteFromBehind: unexpected bytes at %08X, different executable? Feature off.",
        static_cast<unsigned>(game::kBiteFacingCheck));
    return false;
  }
  static const uint8_t kNops[2] = {0x90, 0x90};
  void* target = reinterpret_cast<void*>(game::kBiteFacingJump);
  DWORD old = 0;
  if (!VirtualProtect(target, 2, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(target, kNops, 2);
  VirtualProtect(target, 2, old, &old);
  FlushInstructionCache(GetCurrentProcess(), target, 2);
  Log("BiteFromBehind: on (0043AB69)");
  return true;
}

}  // namespace sf::features
