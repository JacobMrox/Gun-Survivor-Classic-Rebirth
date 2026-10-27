#include "config.h"

#include <windows.h>

namespace sf {

Config g_config;
std::wstring g_dir;

namespace {

const char* g_ini = nullptr;

bool Read(const char* key, bool fallback) {
  return GetPrivateProfileIntA("SurvivorFix", key, fallback ? 1 : 0, g_ini) != 0;
}

}  // namespace

void LoadConfig(const char* iniPath) {
  g_ini = iniPath;
  Config c;

  const bool zombies = Read("Ps1Zombies", true);
  c.ps1ZombieStumble = Read("Ps1ZombieStumble", zombies);
  c.ps1ZombieWalk = Read("Ps1ZombieWalk", zombies);
  c.ps1ZombieWalkAudio = Read("Ps1ZombieWalkAudio", false);
  c.ps1ZombieSpeed = Read("Ps1ZombieSpeed", zombies);
  c.ps1ZombieWalkAnim = Read("Ps1ZombieWalkAnim", zombies);
  c.ps1ZombieBite = Read("Ps1ZombieBite", zombies);
  c.ps1ZombieHit = Read("Ps1ZombieHit", zombies);
  c.ps1ZombieBlend = Read("Ps1ZombieBlend", zombies);
  c.ps1ZombiePose = Read("Ps1ZombiePose", zombies);
  c.ps1ZombieBlood = Read("Ps1ZombieBlood", zombies);
  c.ps1ZombieDeath = Read("Ps1ZombieDeath", zombies);
  c.ps1ZombiePush = Read("Ps1ZombiePush", zombies);
  c.biteFromBehind = Read("BiteFromBehind", zombies);
  // The shadow switch of the window (Ps1ShadowBlend, survivor_pc.cpp) also gives the shadow its PS1 size.
  c.ps1ZombieShadowSize = Read("Ps1ZombieShadowSize", Read("Ps1ShadowBlend", true));
  c.ps1ZombieBackFall = Read("Ps1ZombieBackFall", false);

  const bool chase = Read("Ps1ZombieChase", true);
  c.ps1ZombieNavigation = Read("Ps1ZombieNavigation", chase);
  c.ps1ZombieWorld = Read("Ps1ZombieWorld", chase);
  c.ps1ZombieHeightSupport = Read("Ps1ZombieHeightSupport", false);

  const bool movement = Read("Ps1PlayerMovement", true);
  c.ps1PlayerMove = Read("Ps1PlayerMove", movement);
  c.ps1PlayerWalls = Read("Ps1PlayerWalls", movement);
  c.ps1ScriptedAccess = Read("Ps1ScriptedAccess", false);
  c.ps1PlayerHeightSupport = Read("Ps1PlayerHeightSupport", false);
  c.ps1HeadBob = Read("Ps1HeadBob", movement);

  const bool view = Read("Ps1PlayerView", true);
  c.ps1View = Read("Ps1View", view);
  c.ps1WakeView = Read("Ps1WakeView", view);
  c.ps1DamageView = Read("Ps1DamageView", view);

  c.ps1PlayerDeath = Read("Ps1PlayerDeath", true);
  c.ps1DoorFade = Read("Ps1DoorFade", true);
  c.ps1TitleFade = Read("Ps1TitleFade", false);
  c.ps1Cadence = Read("Ps1FrameRate", true);

  c.inputDeviceFix = Read("InputDeviceFix", true);
  c.exitCrashFix = Read("ExitCrashFix", true);
  c.hudConditionBounds = Read("HudConditionBounds", true);
  g_config = c;
}

}  // namespace sf
