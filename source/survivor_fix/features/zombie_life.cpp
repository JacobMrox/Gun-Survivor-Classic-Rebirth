// Start of a zombie's life: the native initialisation (4361B0) requests its first animation through
// 00436B40 from 00436B04. That call starts the PS1 per-zombie state (801047B4: walk speed).
#include <intrin.h>

#include "../config.h"
#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "../ps1/tyrant_logic.h"
#include "../ps1/zombie_profile.h"
#include "features.h"

namespace sf::features {

namespace {

// __thiscall emulated with __fastcall: ECX = S, EDX unused.
using RequestAnimFn = int(__fastcall*)(void* s, void* edx, int request, int flag);
RequestAnimFn g_requestAnim = nullptr;
bool g_requestReady = false;

int __fastcall RequestAnimDetour(void* s, void* edx, int request, int flag) {
  const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  if (caller == game::kZombieInitAnimReturn && s) zombie::ObserveZombieVoiceLife(s);
  if (caller == game::kZombieInitAnimReturn && s) {
    const int type = game::Read<int>(s, game::S::kType);
    // Mr. X (22/23) is registered in the shared PS1 world too; his own walk step does not use the
    // zombie walk speed table, so only the life is started.
    if (ps1::IsPcZombie(type) || ps1::IsTyrant(type)) zombie::NativeZombieLife(s);
    if (ps1::IsPcZombie(type)) zombie::BeginWalkSpeedLife(s);  // PS1 801047B4, before the mapper's first use
  }
  return g_requestAnim(s, edx, request, flag);
}

}  // namespace

bool InstallZombieLife() {
  if (g_requestReady) return true;
  const game::ByteSignature checks[] = {
      {game::kZombieInitAnimCall, game::kZombieInitAnimCallBytes, sizeof(game::kZombieInitAnimCallBytes)},
      {game::kRequestAnim + 0x21, game::kRequestAnimStore, sizeof(game::kRequestAnimStore)},
      {game::kRequestAnim + 0x24, game::kRequestAnimCall, sizeof(game::kRequestAnimCall)},
  };
  for (const auto& check : checks) {
    if (!game::CheckBytes(check)) {
      Log("ZombieLife: unexpected bytes at %08X, different executable? Feature off.",
          static_cast<unsigned>(check.address));
      return false;
    }
  }
  g_requestReady = Hook("RequestAnim", game::kRequestAnim, reinterpret_cast<void*>(&RequestAnimDetour),
                        reinterpret_cast<void**>(&g_requestAnim));
  return g_requestReady;
}

}  // namespace sf::features
