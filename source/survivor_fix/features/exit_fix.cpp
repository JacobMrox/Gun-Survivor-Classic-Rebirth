// ExitCrashFix: no crash when the game closes (a defect of the original executable).
//
// The game pumps messages also in the middle of a frame with 0046FEC0 (PeekMessageA, TranslateMessage,
// DispatchMessageA). If the close request arrives that way, WM_CLOSE -> DestroyWindow -> WM_DESTROY
// (00592E70) calls 00593050, which frees all the global objects and sets [005FD0C8] = 0; back from
// 0046FEC0, the room loop calls 00466D50 and crashes at 00466D61 (the same defect at 0x181799 and
// 0x69A5F in the menu loops). Here, if [005FD0C8] was valid before the message and is 0 after it,
// the game has already shut down: the process exits at once with ExitProcess(0).
#include <windows.h>

#include <cstdint>

#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "features.h"

namespace sf::features {

namespace {

using PumpFn = void(__cdecl*)();
PumpFn g_pump = nullptr;

void* Graphics() { return *reinterpret_cast<void* const*>(game::kGraphicsObject); }

void __cdecl PumpDetour() {
  const bool alive = Graphics() != nullptr;
  g_pump();
  if (alive && !Graphics()) {
    Log("ExitCrashFix: the game freed everything during a mid-frame message, clean exit");
    ExitProcess(0);
  }
}

}  // namespace

bool InstallExitCrashFix() {
  const game::ByteSignature sig = {game::kMessagePump, game::kMessagePumpBytes, sizeof(game::kMessagePumpBytes)};
  if (!game::CheckBytes(sig)) {
    Log("ExitCrashFix: unexpected bytes at %08X, feature off", static_cast<unsigned>(game::kMessagePump));
    return false;
  }
  return Hook("MessagePump", game::kMessagePump, reinterpret_cast<void*>(&PumpDetour),
              reinterpret_cast<void**>(&g_pump));
}

}  // namespace sf::features
