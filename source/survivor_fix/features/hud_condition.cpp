// HudConditionBounds: PC-only fix of the CONDITION animation array bound.
// 0042C160 updates a shared index only after > 68 ms. On FINE (16 frames) -> CAUT (14),
// 0042C25E..263 can read index 14/15 before that update. The hook runs after the native
// selection and timing, before the texture lookup; no D3D hook, the FLC files are unchanged.
#include <cstdint>

#include "../game.h"
#include "../hooks.h"
#include "../hud_condition_logic.h"
#include "../log.h"
#include "features.h"

namespace sf::features {

namespace {

void* g_lookup = nullptr;
void* g_afterLookup = reinterpret_cast<void*>(game::kConditionAfterTexture);

bool __cdecl ValidateConditionFrame(void* hud, const void* flc) {
  if (!hud || !flc) return false;
  const auto count = game::Read<int16_t>(flc, game::kFlcFrameCount);
  const auto table = game::Read<const void*>(flc, game::kFlcTextures);
  const auto old = game::Read<int32_t>(hud, 0);
  const auto frame = ConditionFrame(old, count);
  if (frame < 0 || !table) return false;
  if (frame != old) *game::Field<int32_t>(hud, 0) = frame;
  return true;
}

// Mid-function ABI: ESI = HUD, EAX = selected FLC. No FP instructions or live FP
// temporaries in the preceding selection/update. All GPRs and EFLAGS are saved
// around the helper, then the two MOVs are replayed through the MinHook trampoline.
__declspec(naked) void ConditionLookupDetour() {
  __asm {
    pushfd
    pushad
    push eax
    push esi
    call ValidateConditionFrame
    add esp, 8
    test al, al
    jz empty
    popad
    popfd
    jmp dword ptr [g_lookup]
  empty:
    popad
    popfd
    mov edi, 0
    jmp dword ptr [g_afterLookup]
  }
}

}  // namespace

bool InstallHudConditionBounds() {
  const game::ByteSignature signatures[] = {
      {game::kDrawCondition, game::kDrawConditionBytes, sizeof(game::kDrawConditionBytes)},
      {game::kConditionTextureLookup, game::kConditionTextureBytes, sizeof(game::kConditionTextureBytes)},
      {game::kFlcCountGetter, game::kFlcCountGetterBytes, sizeof(game::kFlcCountGetterBytes)}};
  for (const auto& sig : signatures) {
    if (!game::CheckBytes(sig)) {
      Log("HudConditionBounds: unexpected bytes at %08X, feature off", static_cast<unsigned>(sig.address));
      return false;
    }
  }
  return Hook("ConditionTextureIndex", game::kConditionTextureLookup,
              reinterpret_cast<void*>(&ConditionLookupDetour), &g_lookup);
}

}  // namespace sf::features
