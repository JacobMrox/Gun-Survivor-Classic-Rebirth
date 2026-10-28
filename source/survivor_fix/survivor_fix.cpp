#include "survivor_fix.h"

#include <string>

#include "config.h"
#include "features/features.h"
#include "hooks.h"
#include "log.h"

extern int ForceWidth, ForceHeight;  // dllmain.cpp, [Graphics] of config.ini

namespace sf::features {

bool DisplaySize(int* w, int* h) {
  if (ForceWidth <= 0 || ForceHeight <= 0) return false;
  *w = ForceWidth;
  *h = ForceHeight;
  return true;
}

}  // namespace sf::features

namespace {

bool g_attached = false;

std::wstring ModuleDir(HMODULE module) {
  wchar_t path[MAX_PATH];
  const DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
  std::wstring dir(path, len);
  return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

}  // namespace

void SurvivorFix_Attach(HMODULE module, const char* iniPath) {
  if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x00400000)) return;
  // The standalone Survivor Fix (dsound.dll) checks this name and stays off: the same hooks must not be
  // installed twice.
  wchar_t mutexName[64];
  wsprintfW(mutexName, L"Local\\SurvivorFix-%lu", GetCurrentProcessId());
  CreateMutexW(nullptr, FALSE, mutexName);
  g_attached = true;
  sf::g_dir = ModuleDir(module);
  sf::LoadConfig(iniPath);
  // Before the game lists the DirectInput devices: its code has not started yet (d3d8.dll is an import of
  // gun_bio.exe).
  if (sf::g_config.inputDeviceFix) sf::features::ApplyInputDeviceFix();
}

void SurvivorFix_Start(const char* iniPath, bool log) {
  using namespace sf;
  using namespace sf::features;
  static bool started = false;
  if (!g_attached || started) return;
  started = true;
  LoadConfig(iniPath);
  LogOpen((g_dir + L"debug.log").c_str(), log);
  const Config& c = g_config;
  Log("Survivor Fix: zombies %d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d, chase %d/%d, movement %d/%d/%d, camera %d/%d/%d, "
      "death %d, doors %d, frame rate %d",
      c.ps1ZombieStumble, c.ps1ZombieWalk, c.ps1ZombieSpeed, c.ps1ZombieWalkAnim, c.ps1ZombieBite, c.ps1ZombieHit,
      c.ps1ZombieBlend, c.ps1ZombiePose, c.ps1ZombieBlood, c.ps1ZombieDeath, c.ps1ZombiePush, c.ps1ZombieNavigation,
      c.ps1ZombieWorld, c.ps1PlayerMove, c.ps1PlayerWalls, c.ps1HeadBob, c.ps1View, c.ps1WakeView,
      c.ps1DamageView, c.ps1PlayerDeath, c.ps1DoorFade, c.ps1Cadence);
  Log("Survivor Fix: title fade %d, walk audio %d, death audio %d, voice banks %d, "
      "scripted access %d, player support %d, zombie support %d",
      c.ps1TitleFade, c.ps1ZombieWalkAudio, c.ps1ZombieDeathAudio, c.ps1ZombieVoiceBanks,
      c.ps1ScriptedAccess, c.ps1PlayerHeightSupport, c.ps1ZombieHeightSupport);
  if (!HooksInit()) return;

  // The room hook also tells the zombie code when a room changes.
  InstallRoomWalls();
  InstallMovieSkip();
  InstallQuickSave();
  InstallMouseInput();
  InstallTitleFade(); // Verify native frame helpers before InstallScreenFx hooks EndFrame.
  if (c.biteFromBehind) InstallBiteFromBehind();
  if (c.ps1PlayerMove || c.ps1HeadBob || c.ps1DamageView || c.ps1PlayerWalls || c.ps1View || c.ps1WakeView ||
      c.ps1ZombieWorld || c.ps1PlayerDeath) {
    InstallPlayer();
  }
  if (c.ps1PlayerDeath || c.ps1DoorFade) InstallScreenFx();
  if (c.hudConditionBounds) InstallHudConditionBounds();
  if (c.ps1Cadence) InstallPs1Cadence();
  ApplyItemGlitterCadence();
  if (c.ps1ZombieShadowSize) InstallPs1ZombieShadowSize();
  if (c.exitCrashFix) InstallExitCrashFix();
  if (c.ps1ZombieWorld || c.ps1ZombieStumble || c.ps1ZombieBackFall || c.ps1ZombieNavigation || c.ps1ZombieWalk ||
      c.ps1ZombieSpeed || c.ps1ZombieBite || c.ps1ZombiePush || c.ps1ZombieHit || c.ps1ZombieDeath ||
      c.ps1ZombiePose || c.ps1ZombieBlood || c.ps1ZombieBlend) {
    InstallZombies();
  }
  Log("Survivor Fix: ready");
}
