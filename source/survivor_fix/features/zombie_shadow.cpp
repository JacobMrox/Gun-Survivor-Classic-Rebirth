// Ps1ZombieShadowSize: the zombie shadow has the PS1 size. Native GAME shadow branch only:
// no D3D hook, no change to vertex buffers, textures or actors.
// TEST.BIN 80073C5C uses half extents 280; the PC (4361B0) uses 500 for its shared 12-vertex mesh.
// The same vertex buffer draws the blood later: it is never shrunk and the constructor is not patched.
#include "../config.h"
#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "../ps1/shadow_logic.h"
#include "../ps1/zombie_profile.h"
#include "features.h"
#include "../../survivor_pc.h"

namespace sf::features {

namespace {

const float* __cdecl PrepareZombieShadowTransform(void* manager, int index, const float* original) {
  if (!g_config.ps1ZombieShadowSize || !manager || index < 0) return original;
  const auto count = game::Read<int>(manager, 0x14);
  auto** actors = game::Read<uint8_t**>(manager, 0x10);
  if (!actors || count < 1 || count > 30 || index >= count || !actors[index]) return original;
  auto* s = actors[index];
  // This renderer branch is the shared enemy shadow pass, not a zombie-only
  // path. Apply the repaired texture and PS1-sized transform to every live
  // enemy actor which reaches it; S+5C selects the separate blood-pool pass.
  if (game::Read<int>(s, 0x5C) != 0) return original;
  SurvivorPc_ArmEnemyShadowDraw();
  thread_local float matrix[16];
  return ps1::ZombieShadowMatrix(original, matrix) ? matrix : original;
}

void* g_shadowPush = nullptr;
void* g_poolPush = nullptr;

void __cdecl PrepareZombiePoolSurface(void* manager, int index) {
  if (!manager || index < 0) return;
  const auto count = game::Read<int>(manager, 0x14);
  auto** actors = game::Read<uint8_t**>(manager, 0x10);
  if (!actors || count < 1 || count > 30 || index >= count || !actors[index]) return;
  const int type = game::Read<int>(actors[index], 0x14);
  const bool second = type == 0x10 || type == 0x11 || type == 0x22 || type == 0x23 || type == 0x24 || type == 0x25;
  SurvivorPc_ArmEnemyPoolDraw(second ? 2 : 1);
}

// 43C924: EAX = O.matrix, ESI = manager, EDI = slot. Replays the native PUSH matrix / PUSH 100.
// The EAX saved by pushad is at +28. The native SetTransform consumes the scratch synchronously.
__declspec(naked) void ShadowTransformDetour() {
  __asm {
    pushfd
    pushad
    push eax
    push edi
    push esi
    call PrepareZombieShadowTransform
    add esp, 12
    mov [esp+28], eax
    popad
    popfd
    jmp dword ptr [g_shadowPush]
  }
}

// 43CB06: ECX = pool transform, ESI = manager, EDI = actor slot.
__declspec(naked) void PoolTransformDetour() {
  __asm {
    pushfd
    pushad
    push edi
    push esi
    call PrepareZombiePoolSurface
    add esp, 8
    popad
    popfd
    jmp dword ptr [g_poolPush]
  }
}

}  // namespace

bool InstallPs1ZombieShadowSize() {
  const game::ByteSignature checks[] = {
      {game::kZombieShadowBranch, game::kZombieShadowBranchBytes, sizeof(game::kZombieShadowBranchBytes)},
      {game::kZombieShadowPush, game::kZombieShadowPushBytes, sizeof(game::kZombieShadowPushBytes)},
      {game::kZombiePoolPush, game::kZombiePoolPushBytes, sizeof(game::kZombiePoolPushBytes)}};
  for (const auto& sig : checks) {
    if (!game::CheckBytes(sig)) {
      Log("Ps1ZombieShadowSize: unexpected bytes, off");
      return false;
    }
  }
  return Hook("ZombieShadowSize", game::kZombieShadowPush, reinterpret_cast<void*>(&ShadowTransformDetour),
              &g_shadowPush) &&
         Hook("ZombiePoolSurface", game::kZombiePoolPush, reinterpret_cast<void*>(&PoolTransformDetour),
              &g_poolPush);
}

}  // namespace sf::features
