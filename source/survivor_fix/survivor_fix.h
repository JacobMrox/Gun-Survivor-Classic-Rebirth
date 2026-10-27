// Survivor Fix: the behaviour of the PS1 version of Resident Evil Survivor (SLUS_010.87) brought back to the
// PC version (gun_bio.exe): zombies, player movement, camera, death, doors. Every fix is ported from the
// PS1 MIPS code (the addresses are quoted in the comments), hooks the game in memory only after checking the
// expected bytes and stays off on a different executable. Options: config.ini, section [SurvivorFix]
// (survivor_fix/config.h); the drawing of the screen effects is in survivor_pc.cpp.
#pragma once

#include <windows.h>

// From DllMain, before the game's code runs: reads the options and applies the start-up fixes.
void SurvivorFix_Attach(HMODULE module, const char* iniPath);

// After the configuration window: reads the options again and hooks the game. `log` writes debug.log.
void SurvivorFix_Start(const char* iniPath, bool log);
