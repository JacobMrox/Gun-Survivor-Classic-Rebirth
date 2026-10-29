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
  c.ps1ZombieWalkAudio = Read("Ps1ZombieWalkAudio", zombies);
  c.ps1ZombieSpeed = Read("Ps1ZombieSpeed", zombies);
  c.ps1ZombieWalkAnim = Read("Ps1ZombieWalkAnim", zombies);
  c.ps1ZombieBite = Read("Ps1ZombieBite", zombies);
  c.ps1ZombieHit = Read("Ps1ZombieHit", zombies);
  c.ps1ZombieBlend = Read("Ps1ZombieBlend", zombies);
  c.ps1ZombiePose = Read("Ps1ZombiePose", zombies);
  c.ps1ZombieBlood = Read("Ps1ZombieBlood", zombies);
  c.ps1ZombieDeath = Read("Ps1ZombieDeath", zombies);
  c.ps1ZombieDeathAudio = Read("Ps1ZombieDeathAudio", zombies);
  c.ps1ZombieVoiceBanks = Read("Ps1ZombieVoiceBanks", zombies);
  c.ps1ZombiePush = Read("Ps1ZombiePush", zombies);
  c.biteFromBehind = Read("BiteFromBehind", zombies);
  // The shadow switch of the window (Ps1ShadowBlend, survivor_pc.cpp) also gives the shadow its PS1 size.
  c.ps1ZombieShadowSize = Read("Ps1ZombieShadowSize", Read("Ps1ShadowBlend", true));
  c.ps1ZombieBackFall = Read("Ps1ZombieBackFall", false);

  const bool chase = Read("Ps1ZombieChase", true);
  c.ps1ZombieNavigation = Read("Ps1ZombieNavigation", chase);
  c.ps1ZombieWorld = Read("Ps1ZombieWorld", chase);
  c.ps1ZombieHeightSupport = Read("Ps1ZombieHeightSupport", chase);

  const bool movement = Read("Ps1PlayerMovement", true);
  c.ps1PlayerMove = Read("Ps1PlayerMove", movement);
  c.ps1PlayerWalls = Read("Ps1PlayerWalls", movement);
  c.ps1ScriptedAccess = Read("Ps1ScriptedAccess", movement);
  c.ps1PlayerHeightSupport = Read("Ps1PlayerHeightSupport", movement);
  c.ps1HeadBob = Read("Ps1HeadBob", movement);

  const bool view = Read("Ps1PlayerView", true);
  c.ps1View = Read("Ps1View", view);
  c.ps1WakeView = Read("Ps1WakeView", view);
  c.ps1DamageView = Read("Ps1DamageView", view);

  c.ps1PlayerDeath = Read("Ps1PlayerDeath", true);
  c.ps1DoorFade = Read("Ps1DoorFade", true);
  c.ps1TitleFade = Read("Ps1TitleFade", true);
  c.itemApproachPrompt = GetPrivateProfileIntA("Fixes", "ItemApproachPrompt", 1, iniPath) != 0;
  c.lethalZombieHeadshots = GetPrivateProfileIntA("Fixes", "LethalZombieHeadshots", 0, iniPath) != 0;
  char uncensored[16] = {};
  GetPrivateProfileStringA("Fixes", "UncensoredHeadshots", "", uncensored, sizeof(uncensored), iniPath);
  if (uncensored[0]) {
    c.detachedZombieHeads = std::atoi(uncensored) != 0;
  } else {
    char exploding[16] = {};
    GetPrivateProfileStringA("Fixes", "ExplodingZombieHeads", "", exploding, sizeof(exploding), iniPath);
    c.detachedZombieHeads = exploding[0]
        ? std::atoi(exploding) != 0
        : GetPrivateProfileIntA("Fixes", "DetachedZombieHeads", 1, iniPath) != 0;
  }
  char bloodColor[32] = {};
  GetPrivateProfileStringA("Fixes", "BloodColor", "255,255,255", bloodColor, sizeof(bloodColor), iniPath);
  int bloodR = 255, bloodG = 255, bloodB = 255;
  if (sscanf_s(bloodColor, "%d,%d,%d", &bloodR, &bloodG, &bloodB) == 3) {
    bloodR = (std::max)(0, (std::min)(255, bloodR));
    bloodG = (std::max)(0, (std::min)(255, bloodG));
    bloodB = (std::max)(0, (std::min)(255, bloodB));
    c.bloodColor = (uint32_t(bloodR) << 16) | (uint32_t(bloodG) << 8) | uint32_t(bloodB);
  }
  c.ps1Cadence = Read("Ps1FrameRate", true);

  c.inputDeviceFix = Read("InputDeviceFix", true);
  c.exitCrashFix = Read("ExitCrashFix", true);
  c.hudConditionBounds = Read("HudConditionBounds", true);
  g_config = c;
}

}  // namespace sf
