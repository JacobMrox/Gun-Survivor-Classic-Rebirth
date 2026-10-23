// PS1 logic of the zombie lunge (state 0xB) and of the bitten player's sequence.
//
// Source: PS1 zombie code bank at 80100000 (ENEMY_SLOT40) and TEST.BIN, read in Ghidra on 2026-10-16.
//   - 80102828 (state 4): once per band entry, band 2 rng < 0x14, band 3 rng < 0x33, plus
//     80100924(E,0x28) (sector in front of the zombie): state 0xB with 0x1C.
//     Bits of E+35C: 4 = band 2 roll, 2 = band 3 roll; every band clears the bits of the
//     others (masks ~0x17/~0x1B/~0x1D/~0x1E).
//   - 80103868 (state 0xB): 0x1C at 4 x E+866; at the end of the clip a grab if distance < 0x340,
//     in the player's cone (E+320 & 0x40), bit 2 of E+35C and 80100628; otherwise 0x1F at
//     2 x E+866 and back to state 4. Grab: 0x1D with the zombie brought to 0x23 * 20 in front
//     of the player, turning around them by 0x37 per frame; then 0x1E, bite at Q1 cursor = start + 4.
//   - 800830F8: bitten player sequence (12 frames of jolt, mode 0 rotation at 0x7A per frame,
//     then the zombie is pushed back).
#pragma once

namespace ps1 {

constexpr int kSelLunge = 0x1C;
constexpr int kSelLungeGrab = 0x1D;
constexpr int kSelLungeBite = 0x1E;
constexpr int kSelLungeMiss = 0x1F;
constexpr int kSelKnockback = 0x20;  // 80101474 with E+320 & 0x1000

constexpr int kLungeBand2Rng = 0x14;
constexpr int kLungeBand3Rng = 0x33;
constexpr int kLungeBand2Bit = 4;
constexpr int kLungeBand3Bit = 2;
constexpr float kLungeGrabDistance = 0x340;
constexpr float kLungeHoldDistance = 0x23 * 20;  // 700
constexpr int kLungeSlideStep = 0x37;            // angle units per frame
constexpr int kLungeBiteFrame = 4;               // Q1 cursor - start
constexpr int kLungeSpeedFactor = 4;
constexpr int kLungeMissSpeedFactor = 2;

// 800830F8: frames 0..11 jolt, at 12 the rotation towards the zombie starts.
constexpr int kBiteShakeFrames = 12;
constexpr int kBiteTurnStep = 0x7A;  // 80078A58 mode 0

struct LungeBits {
  int bits = 0;  // E+35C, only bits 2 and 4
};

// 80102828: bit mask for the current band (1..4; 0 = none).
void ApplyBandMask(LungeBits& b, int band);

// 80102828: lunge roll (uses up the band's roll even when it fails).
bool ShouldStartLunge(LungeBits& b, int band, int rngByte, bool zombieSectorOk);

// 80103868, end of 0x1C: grab (true) or missed lunge (false).
bool LungeGrabs(const LungeBits& b, float distance, bool inPlayerCone, bool gateOk);

// 80103868: lunge speed (E+866 * factor, limit 127 like 80103868).
int LungeSpeed(int walkSpeed, int factor);

// 80103868 substates 3/4: angle offset (4096 units) that drops by 0x37 per frame down to 0.
// Returns the offset to use in this frame.
struct GrabSlide {
  int offset = 0;
  bool active = false;
};
GrabSlide StartGrabSlide(int offsetUnits);
int StepGrabSlide(GrabSlide& s);

}  // namespace ps1
