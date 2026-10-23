// PS1 logic of the fades and of the player's death (full screen effects).
//
// Source: TEST.BIN (8006C118), read on 2026-10-23.
//   - 80090BF0(x, n): fade to black, 800A55A2 = n << 8 | 6, level 800BBCE0 = 0.
//     80090B90(x, n): from black, 800A55A2 = n << 8 | 5, level 0xFF (0x4FF if 800A8954, then cleared).
//   - 80090C90, every frame before drawing (8006D4D0): to black level + n, beyond 0xFE it stays
//     0xFF and 800A55A2 = 4; from black level - n, at <= 0 it stays 0 and 800A55A2 = 0. 80090D4C draws the
//     subtractive box (attribute 0x60000000, 80090AB8) with the level's colour while 800A55A2 & 4.
//   - 8008369C, player state 3 (table 800A28E0), step 800A28CC:
//     0  trail 3 (800A0CB0 = 1, 800A0CB4 = 3: the zoom 80085814), colour 800BB158 = 0x28, scale 800BB164 =
//        0x1140, subtract 800BB15C = 0, controls off (80062DEC = 0), 800BB074 = 0x18, 800BB078 = 0x50,
//        800BB07C = 0xB4.
//     1  while 800BB074: -1 and pitch 800A865C - 0x20; eyes 800A8634 + 0x50 until > -200; the player
//        slides by (0, 0, 800BB07C) rotated by their angles (8008286C, with the walls of 8007CB9C), then
//        800BB07C - 1 - 800BB074.
//     2  800BB074 = 0x5A.   3  counts down to 0, then 800BB074 = 0x3C.
//     4  colour + 4 (at most 0x7F), scale + 0xE; with the colour above 0x60 the view twist 800B3F00
//        (GsRVIEW2.rz, 4096 = 1 degree) + 800BB168, which grows by 0x400 up to 0x2D000; with 800BB074 < 0x20
//        subtract (0x20 - 800BB074) * 8 (at most 0xFF); at 800BB074 = 0 the next step, otherwise - 1.
//     5  80090BF0(0, 0x10).   6  with 800A55A2 = 4 trail off (800A0CB0 = 0) and step 7: YOU DIED.
//     Up to step 6 included 800825E4 asks for the red flash (80082608) every frame.
//     7  YOU DIED and 80090B90(0, 8), 800BB074 = 0.   8  (goes on into 9) at 800A55A2 = 0 step 9.
//     9  800BB074 + 1 up to 0x28; once 0x28 is reached and a button has just been pressed, 80090BF0(0, 8).
//     10 at 800A55A2 = 4 the Continue, 11: 80090B90(0, 8).
#pragma once

namespace ps1 {

struct Fade {
  int state = 0;  // 800A55A2: speed << 8 | 6 to black, | 5 from black, 4 black and still, 0 off
  int level = 0;  // 800BBCE0
};

void FadeOut(Fade& f, int speed);                         // 80090BF0
void FadeIn(Fade& f, int speed, bool longHold = false);   // 80090B90 (longHold: 800A8954)
void StepFade(Fade& f);                                   // 80090C90
// Level to subtract from the frame (0 if 80090D4C does not draw the box).
int FadeDrawLevel(const Fade& f);
inline bool FadeBlack(const Fade& f) { return f.state == 4; }        // 800A55A2 == 4
inline bool FadeIdle(const Fade& f) { return f.state == 0; }         // 800A55A2 == 0
inline bool FadeDrawn(const Fade& f) { return (f.state & 4) != 0; }  // 800A55A2 & 4

constexpr int kEyeRest = -0x640;  // 800A8634 at rest (800787C8: table - 0x640)

struct PlayerDeath {
  int step = 0;          // 800A28CC
  int counter = 0;       // 800BB074
  int fall = 0;          // 800BB078
  int slide = 0;         // 800BB07C
  int eye = kEyeRest;    // 800A8634
  int pitch = 0;         // sum of the steps of 800A865C (4096 per turn)
  int trailColor = 0;    // 800BB158
  int trailScale = 0;    // 800BB164 (4096 = 1)
  int subtract = 0;      // 800BB15C
  int twist = 0;         // 800B3F00 (4096 = 1 degree)
  int twistSpeed = 0;    // 800BB168
  bool trail = false;    // 800A0CB0
};

// What one death frame asks of the game.
struct DeathFrame {
  int slide = 0;        // movement (0, 0, slide) of the player along their axes
  bool redFlash = false;
};

void StartPlayerDeath(PlayerDeath& d, int eye = kEyeRest);
// One frame of 8008369C (steps 0-6); the fade then has to be advanced with StepFade.
DeathFrame StepPlayerDeath(PlayerDeath& d, Fade& f);
inline bool PlayerDeathDone(const PlayerDeath& d) { return d.step >= 7; }
// 800789F8, from the player update (800785C0 at 800788CC) when 800A865C != 0: the pitch goes back towards 0
// by 0x10 per frame. In the real death (DuckStation 2026-10-23, bitten in 07/00) it runs together with
// 8008369C: net -0x10 per frame down to -256, then back to 0 in step 3 (the view on the ground looks level).
void RecoverPitch(int& pitch);

// YOU DIED (steps 7-10): minimum frames before a button closes the screen.
constexpr int kYouDiedMinFrames = 0x28;
constexpr int kYouDiedFadeSpeed = 8;
// Step 7: before 80090B90(0, 8) the PS1 loads YOU DIED from the CD on a black screen. Measured in DuckStation:
// 52 frames in a death bitten in the 07/00 alley, 37 in a death started by writing state 3 (2026-10-23).
// ASSUMPTION: the real death is used; it depends on the CD.
constexpr int kYouDiedLoadFrames = 52;
// YOU DIED (8007A874, every frame of steps 8-10): camera 800B3EF0 goes from -4000 to -2501 at +15 per frame
// (100 frames, the zoom); then 800B6060 + 1 per frame, the low rows of the words scroll by
// 0x200 / (800B6060 + 0x10) (the reveal) and beyond 0x3F the wave: front 800B605C + 1 per frame.
constexpr int kYouDiedZoomFrames = 100;
// Counter of phase 0 of 00591900 that gives the final PS1 size (2026-10-23: words 23% of the screen
// high on the PS1, 0.00517 per unit on the PC). ASSUMPTION: measured on screenshots, not in the code.
constexpr float kYouDiedFinalCounter = 44.5f;
constexpr int kYouDiedRevealFrames = 0x40;
// Step 0xB: AEX.DAT (8007ABF0) on a black screen before 80090B90(0, 8). Measured in DuckStation on
// 2026-10-23: 30 frames. ASSUMPTION: it depends on the CD like kYouDiedLoadFrames.
constexpr int kContinueLoadFrames = 30;
// Continue (8007B2A8): 800B6070 starts from 0x275 and drops by 1 per frame; the digit is (800B6070 / 0x3C) % 10,
// so it changes every 60 frames (2 s), with the light going up and down by 8 per frame in the same period.
constexpr int kContinueDigitFrames = 0x3C;
// End of the countdown (8007B2A8): from 800B6070 = -0x20 GAME OVER rises by 2 per frame up to 0x80 (64 frames),
// at -0x14C the screen ends (800B6078 = 1) and 8008369C step 0xD does 80090BF0(0, 8) before the title.
constexpr int kGameOverWaitFrames = 0x20;
constexpr int kGameOverFadeSpeed = 4;  // opacity + 4 per frame: 64 frames, like the light from 0 to 0x80 at +2
constexpr int kGameOverEndFrames = 0x14C;
constexpr int kDeathFadeOutSpeed = 0x10;

}  // namespace ps1
