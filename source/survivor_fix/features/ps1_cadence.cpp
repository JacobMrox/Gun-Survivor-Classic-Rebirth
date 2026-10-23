// Ps1Cadence: the game runs at the PS1 rate (two NTSC vertical blanks per frame, about 29.91 logic updates/s).
// Only the native gameplay wait is replaced, keeping the real timeGetTime and all the later logic.
// MIPS 8001107C/800255F8/80025770: VSync(2), not a speed or animation multiplier.
#include <windows.h>

#include "../config.h"
#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "../ps1/cadence_logic.h"
#include "features.h"

namespace sf::features {

namespace {

void* g_nativeWait = nullptr;
void* g_afterWait = reinterpret_cast<void*>(game::kPcCadenceAfterWait);
ps1::NtscVBlankCadence g_timeline;
void* g_context = nullptr;
int64_t g_frequency = 0;

bool __cdecl WaitPs1Cadence(void* context) {
  if (!g_config.ps1Cadence || !context || game::Read<uint8_t>(context, 0x41) != 3) {
    g_timeline.Reset();
    g_context = nullptr;
    return false;
  }
  if (g_context != context) {
    g_timeline.Reset();
    g_context = context;
  }
  LARGE_INTEGER qpc;
  if (!QueryPerformanceCounter(&qpc)) return false;
  const auto waitUntil = [&qpc](int64_t deadline) {
    if (deadline <= 0) return false;
    while (qpc.QuadPart < deadline) {
      // The native limiter already busy-waits. Sleep(1) can sleep one 15.6 ms host quantum,
      // creating 31/47 ms pacing even with the correct mean. No Windows timer changes.
      YieldProcessor();
      if (!QueryPerformanceCounter(&qpc)) return false;
    }
    return true;
  };
  // Two distinct waits: the current virtual VBlank is read again after the first.
  // A scheduler oversleep must not skip the second current+1 wait.
  if (!waitUntil(g_timeline.Begin(qpc.QuadPart, g_frequency)) ||
      !waitUntil(g_timeline.SecondDeadline(qpc.QuadPart)) || !g_timeline.Finish(qpc.QuadPart)) {
    g_timeline.Reset();
    return false;
  }
  return true;
}

// Native 46A5DD: ESI = H, EDI = timeGetTime. No live FP in the preceding integer wait.
// All GPRs and flags are saved. Native/other H+41 use the original trampoline; the PS1 path jumps to
// 46A5F7 CALL EDI, keeping the real millisecond clock store at 46A603 and all the actions.
__declspec(naked) void CadenceWaitDetour() {
  __asm {
    pushfd
    pushad
    push esi
    call WaitPs1Cadence
    add esp, 4
    test al, al
    jz native_wait
    popad
    popfd
    jmp dword ptr [g_afterWait]
  native_wait:
    popad
    popfd
    jmp dword ptr [g_nativeWait]
  }
}

}  // namespace

bool InstallPs1Cadence() {
  const game::ByteSignature checks[] = {
      {game::kPcCadenceWait, game::kPcCadenceWaitBytes, sizeof(game::kPcCadenceWaitBytes)},
      {game::kPcCadenceAfterWait, game::kPcCadenceAfterBytes, sizeof(game::kPcCadenceAfterBytes)}};
  for (const auto& sig : checks) {
    if (!game::CheckBytes(sig)) {
      Log("Ps1Cadence: unexpected bytes, native wait kept");
      return false;
    }
  }
  LARGE_INTEGER f;
  if (!QueryPerformanceFrequency(&f) || f.QuadPart < 1000 || f.QuadPart > 1000000000) {
    Log("Ps1Cadence: incompatible performance counter, off");
    return false;
  }
  g_frequency = f.QuadPart;
  return Hook("Ps1CadenceWait", game::kPcCadenceWait, reinterpret_cast<void*>(&CadenceWaitDetour), &g_nativeWait);
}

}  // namespace sf::features
