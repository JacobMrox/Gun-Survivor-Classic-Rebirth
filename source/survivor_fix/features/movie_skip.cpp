// Native movie completion (005818A0), on the game's thread. The caller stops and releases the graph
// through 00580A30 just as at natural completion; no COM proxy or detached worker is needed.
#include <windows.h>
#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "../movie_skip_logic.h"
#include "features.h"
extern bool bSkipMovies;  // Classic Rebirth configuration, read after the configuration window.
namespace sf::features {
namespace {
using MovieInitFn = int(__fastcall*)(void*, void*, const char*); // ECX=movie, path on stack, RET4.
using MovieDoneFn = unsigned char(__cdecl*)(); // No arguments, completion in AL, plain RET.
MovieInitFn g_init = nullptr;
MovieDoneFn g_done = nullptr;
MovieSkipGate g_gate;
unsigned Keys() {
  return ((GetAsyncKeyState(VK_RETURN) & 0x8000) ? 1u : 0u) |
         ((GetAsyncKeyState(VK_SPACE) & 0x8000) ? 2u : 0u);
}
bool Foreground() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}
int __fastcall MovieInit(void* movie, void* edx, const char* path) {
  g_gate.Begin(Keys()); // Do not inherit the held title confirmation as a new skip press.
  return g_init(movie, edx, path);
}
unsigned char __cdecl MovieDone() {
  const bool completed = g_done() != 0;
  const bool skip = g_gate.Poll(bSkipMovies, Foreground(), Keys(), completed);
  if (skip && !completed) Log("MovieSkip: %s; native completion and cleanup", bSkipMovies ? "automatic" : "new key press");
  return skip ? 1 : 0;
}
}  // namespace
bool InstallMovieSkip() {
  const game::ByteSignature checks[] = {
    {game::kMovieInit, game::kMovieInitBytes, sizeof(game::kMovieInitBytes)},
    {game::kMovieDone, game::kMovieDoneBytes, sizeof(game::kMovieDoneBytes)}};
  for (const auto& sig : checks) if (!game::CheckBytes(sig)) {
    Log("MovieSkip: unexpected bytes, native movie playback unchanged");
    return false;
  }
  return Hook("MovieInit", game::kMovieInit, reinterpret_cast<void*>(&MovieInit), reinterpret_cast<void**>(&g_init)) &&
         Hook("MovieDone", game::kMovieDone, reinterpret_cast<void*>(&MovieDone), reinterpret_cast<void**>(&g_done));
}
}  // namespace sf::features
