// Survivor Fix options, read from config.ini, section [SurvivorFix].
//
// Gameplay fixes are enabled by default and do not add switches to the configuration window.
// Existing config.ini keys remain available for diagnostics: groups default to on, and each fix
// defaults to its group. The unsupported backward-fall trigger remains off.
// Crash fixes (InputDeviceFix, ExitCrashFix, HudConditionBounds) default to on.
#pragma once

#include <string>

namespace sf {

struct Config {
  // Zombies (Ps1Zombies) ----------------------------------------------------------------------------
  // Stumble and fall forward while chasing (80102828, 80103C6C): 23 -> 10 -> 11.
  bool ps1ZombieStumble = true;
  // Backward fall after the 0x20 gesture. Off: its trigger is not the PS1 one yet (the PS1 rolls at the
  // end of the grab), the backward fall after the bite stays (it belongs to Ps1ZombieBite).
  bool ps1ZombieBackFall = false;
  // One walk per zombie (02 or 03) like on the PS1, without the PC's jerky switches.
  bool ps1ZombieWalk = true;
  // Original locomotion audio, independent of animation restarts.
  bool ps1ZombieWalkAudio = true;
  // PS1 walk speed: random base 13..25 units/frame per zombie, growing with the distance from the player
  // (801047B4, 80104D5C).
  bool ps1ZombieSpeed = true;
  // PS1 walk animation speed: 1x-4x by the zombie's base (80104E78). Needs Ps1ZombieSpeed.
  bool ps1ZombieWalkAnim = true;
  // PS1 bite: the zombie attacks only after 16 updates in a row in the +/-48 degree cone in front of the
  // player within 1000, one at a time (80102828, 800824B0, 80100628); lunge, grab and bite sequence.
  bool ps1ZombieBite = true;
  // PS1 reaction to hits (80101474): ~88% none, 0x15/07/0B/06/0A, 0x20 in state 6.
  bool ps1ZombieHit = true;
  // PS1 blend when the ported clips change (8007F6AC).
  bool ps1ZombieBlend = true;
  // PS1 pose over the animation: head towards the player (E+364), torso bend on a hit (E+368).
  bool ps1ZombiePose = true;
  // PC blood on a hit with the PS1 duration, growth and point (80070628, 80098608 / 800987F8).
  bool ps1ZombieBlood = true;
  // PS1 zombie death (80101700, 801032CC): clip from the side hit, last pose held.
  bool ps1ZombieDeath = true;
  // Restore the lethal-hit voice independently of the managed death clip.
  bool ps1ZombieDeathAudio = true;
  // Original encounter-controlled voice banks, with native fallback for unmatched profiles.
  bool ps1ZombieVoiceBanks = true;
  // PS1 separation between zombie and player (circle 600 + 380 with 80081FF0).
  bool ps1ZombiePush = true;
  // Monsters also bite from behind (the PC filtered out the hits from behind).
  bool biteFromBehind = true;
  // PS1 size of the zombie shadow (80073C5C). Follows Ps1ShadowBlend, the shadow blending setting.
  bool ps1ZombieShadowSize = true;

  // Zombie chase (Ps1ZombieChase) -------------------------------------------------------------------
  // PS1 chase with the ROOMENV walls (state 4): the zombie goes around the walls.
  bool ps1ZombieNavigation = true;
  // Shared PS1 planar world (80081618, 8007F404): contacts and enemy walls like on the PS1.
  bool ps1ZombieWorld = true;
  // Retain native mesh support after the PS1 planar step.
  bool ps1ZombieHeightSupport = true;

  // Player movement (Ps1PlayerMovement) -------------------------------------------------------------
  // PS1 player movement (800771AC): forward 80, running 150, backwards 60 per frame, fixed from the first
  // frame; rotation with the PS1 ramp and braking on release.
  bool ps1PlayerMove = true;
  // PS1 walls for the player (800785C0 -> 8007CB9C): a point against the room's segments instead of the PC
  // object boxes (sphere of radius 650).
  bool ps1PlayerWalls = true;
  // Native approach inside original type-3 scripted interaction ranges.
  bool ps1ScriptedAccess = true;
  bool ps1PlayerHeightSupport = true;  // Preserve native floor boxes.
  // PS1 view bob (800785C0): the eye rises up to 68 (112 running) by the table.
  bool ps1HeadBob = true;

  // Camera (Ps1PlayerView) --------------------------------------------------------------------------
  // PS1 view: vertical field of view 47.9 degrees (H = 270, Hor+ on wide screens) and the look 9.46
  // degrees down like (0, 500, -3000).
  bool ps1View = true;
  // Opening wake-up (07/04): the view follows the PS1 curve and ends level.
  bool ps1WakeView = true;
  // PS1 view jolt when the zombie bites (800830F8): head back and to the side for 12 frames.
  bool ps1DamageView = true;

  // PS1 player death (8008369C): the view falls to the ground, red flash, zoom of the previous frame, twist
  // and black; then YOU DIED, Continue and GAME OVER with the PS1 timing.
  bool ps1PlayerDeath = true;
  // PS1 door fades (8009016C): the room goes to black in 16 frames, the door comes out of it in 16, the
  // new room in 2.
  bool ps1DoorFade = true;
  // Original title confirmation flash and fade.
  bool ps1TitleFade = true;
  // Optional restoration of the PS1 lethal head-part rule, exposed in the configuration window.
  bool lethalZombieHeadshots = false;
  // Visual decapitation for lethal headshots. This also gates the detached/falling head effect.
  bool detachedZombieHeads = true;
  // The game runs at the PS1 rate: two NTSC vertical blanks per frame, about 29.91 logic updates/s (VSync(2)).
  bool ps1Cadence = true;

  // Crash fixes, always on.
  bool inputDeviceFix = true;
  bool exitCrashFix = true;
  bool hudConditionBounds = true;

  // Fixed PS1 tuning (not options).
  // S+40 of the PS1 clips at 1x: the PC mapper uses 4.0 for the bite (0.86 s), the PS1 does it in 1.23 s
  // (36 frames + 1 of blend): 2.93.
  float biteSpeed = 2.93f;
  float fallSpeed = 2.93f;  // 8008035C(E,0xC,0x23,4): 1x PS1 like the other clips
  float getUp1Speed = 2.93f;
  float getUp2Speed = 2.93f;
  float backFallSpeed = 2.93f;
  float supineSpeed = 2.93f;
  float sitUpSpeed = 2.93f;
  float walkAnimSpeed = 2.93f;  // S+40 of the walk worth the normal PS1 speed (the PC uses 8.0 for all)
  int exitFrame = 8;            // command of 0x23 at which the outcome is decided (80103C6C)
  int backFallFrame = 10;       // command of 0x20 at which the backward fall is rolled (80102D74)
  // Backward push of 0x20 and 08 (80102D74: 5 x 20, 18, ..., 2 = 550 units), 1.0 = PS1.
  float backFallPush = 1.0f;
  float navProbeDistance = 2000.0f;     // probe ahead and to the sides (PS1: 100 * 0x14)
  float navTurnStep = 1.93f;            // degrees per PS1 frame while going around (PS1: 0x16)
  float navRateScale = 1.0f;            // PS1 frames per PC enemy logic update (~30 / 29.4)
  float navAimDistance = 3000.0f;       // distance of the aim point given to the PC enemy logic
  float navPlayerWallMargin = 1000.0f;  // walls ignored if the player is closer than this
};

extern Config g_config;
extern std::wstring g_dir;  // game folder, with a trailing '\'

void LoadConfig(const char* iniPath);

}  // namespace sf
