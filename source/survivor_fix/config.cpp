#include "config.h"

#include <windows.h>

#include "../debug.h"

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
  c.ps1Crows = Read("Ps1Crows", true);
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
  // [Fixes] is canonical; preserve existing [SurvivorFix] configurations.
  const int legacyFlash = GetPrivateProfileIntA("SurvivorFix", "Ps1ShootFlash", 0, g_ini);
  const int legacyFrames = GetPrivateProfileIntA("SurvivorFix", "Ps1ShootFlashFrames", 2, g_ini);
  c.ps1ShootFlash = GetPrivateProfileIntA("Fixes", "Ps1ShootFlash", legacyFlash, g_ini) != 0;
  c.ps1ShootFlashFrames = GetPrivateProfileIntA("Fixes", "Ps1ShootFlashFrames", legacyFrames, g_ini);
  if (c.ps1ShootFlashFrames < 1) c.ps1ShootFlashFrames = 1;
  if (c.ps1ShootFlashFrames > 5) c.ps1ShootFlashFrames = 5;
  c.itemApproachPrompt = GetPrivateProfileIntA("Fixes", "ItemApproachPrompt", 1, iniPath) != 0;
  // XInput pad: XInputDeadZone is a percentage of stick travel (0-95).
  c.xinputPad = Read("XInput", true);
  c.xinputDeadZone = static_cast<float>(GetPrivateProfileIntA("SurvivorFix", "XInputDeadZone", 50, iniPath)) / 100.0f;
  if (c.xinputDeadZone < 0.0f) c.xinputDeadZone = 0.0f;
  if (c.xinputDeadZone > 0.95f) c.xinputDeadZone = 0.95f;
  // [Gamepad] bindings: a missing entry keeps the shipped default, an empty one unbinds
  // it, and an unknown name is ignored so a typo cannot silently remove a working button.
  pad::DefaultBindings(c.padBindings, pad::kSourceCount);
  for (unsigned i = 0; i < pad::kSourceCount; ++i) {
    const char* source = pad::SourceName(static_cast<pad::Source>(i));
    char value[64] = {};
    GetPrivateProfileStringA("Gamepad", source, "", value, sizeof(value), iniPath);
    if (!value[0]) continue;
    pad::Binding binding;
    if (pad::ParseBinding(value, &binding)) {
      c.padBindings[i] = binding;
    } else {
      LogLine("[PAD] Unknown binding '%s' for %s; keeping the default", value, source);
    }
  }
  const int aimSpeed = GetPrivateProfileIntA("Gamepad", "AimSpeed", 20, iniPath);
  c.padAimSpeed = static_cast<float>(aimSpeed < 0 ? 0 : (aimSpeed > 200 ? 200 : aimSpeed));
  c.padAimInvertY = GetPrivateProfileIntA("Gamepad", "AimInvertY", 0, iniPath) != 0;
  c.ds4 = Read("DualShock4", true);
  c.ds4Speaker = Read("Ds4Speaker", true);
  c.ds4Rumble = Read("Ds4Rumble", true);
  c.ds4SpeakerVolume = GetPrivateProfileIntA("SurvivorFix", "Ds4SpeakerVolume", 80, iniPath);
  if (c.ds4SpeakerVolume < 0) c.ds4SpeakerVolume = 0;
  if (c.ds4SpeakerVolume > 100) c.ds4SpeakerVolume = 100;
  GetPrivateProfileStringA("SurvivorFix", "Ds4SpeakerDevice", "", c.ds4SpeakerDevice,
                           sizeof(c.ds4SpeakerDevice), iniPath);
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
  c.debugEnable = Read("DebugEnable", false);

  c.inputDeviceFix = Read("InputDeviceFix", true);
  c.exitCrashFix = Read("ExitCrashFix", true);
  c.hudConditionBounds = Read("HudConditionBounds", true);
  g_config = c;
}

}  // namespace sf
