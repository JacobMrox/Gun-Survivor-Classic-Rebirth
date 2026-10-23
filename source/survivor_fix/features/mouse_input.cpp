// Restore the standalone mouse drain: the game reads at most 16 events in 00406C70.
// Growing its buffer without repeating the native read queues old movement and delays the crosshair.
// Both the game and Classic Rebirth receive the unchanged batches through the existing mouse bridge.
#include <windows.h>
#include "../../survivor_pc.h"
#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "../mouse_drain_logic.h"
#include "features.h"
namespace sf::features {
namespace {
using ReadFn = int(__fastcall*)(void*, void*, int); // ECX=input, index on stack, RET4.
ReadFn g_read = nullptr;
unsigned g_peak = 1;
int __fastcall ReadMouse(void* input, void* edx, int index) {
  const auto* base = static_cast<const unsigned char*>(input);
  if (!base || index < 0 || index >= game::Read<int>(base, game::kInputDeviceCountOffset) ||
      base[game::kInputDeviceTypes + index] != game::kInputDeviceMouse) return g_read(input, edx, index);
  const auto result = DrainMouse([&] {
    SurvivorPc_ResetMouseRead();
    const int nativeResult = g_read(input, edx, index);
    return MouseBatch{nativeResult, SurvivorPc_MouseMotionEvents()};
  });
  if (result.reads > g_peak) {
    g_peak = result.reads;
    Log("MouseInput: drained %u native batches in one update", g_peak);
  }
  return result.result;
}
}  // namespace
bool InstallMouseInput() {
  if (!SurvivorPc_MouseBufferSize()) return true;
  const game::ByteSignature sig = {game::kReadInputDevice, game::kReadInputDeviceBytes, sizeof(game::kReadInputDeviceBytes)};
  if (!game::CheckBytes(sig)) { Log("MouseInput: unexpected device reader, native read unchanged"); return false; }
  return Hook("ReadInputDevice", game::kReadInputDevice, reinterpret_cast<void*>(&ReadMouse), reinterpret_cast<void**>(&g_read));
}
}  // namespace sf::features
