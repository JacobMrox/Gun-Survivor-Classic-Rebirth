// PS1 logic of the zombie stumble/fall, translated from the original MIPS code.
//
// Source: SLUS_010.87, zombie code bank at 80100000.
//   - distance:  8007F4F4 (E+868, planar, integer)
//   - decision:  80102828 (state 4, chase)
//   - sector:    80100924 -> 8010078C
//   - outcome:   80103C6C (state C, after sequence 23)
//
// This module is pure: no PC addresses, no game memory.
// Every point not yet verified in the MIPS code is marked ASSUMPTION.
#pragma once

#include <cstdint>

namespace ps1 {

// PS1 animation selectors (same indices in the ENEM00.NAB/NAC data of the PC version).
constexpr int kSelFall = 0x23;     // arms forward, falls forward
constexpr int kSelContact = 0x24;  // close outcome (grab/attack)
constexpr int kSelGetUp1 = 0x10;   // from prone to hands and knees
constexpr int kSelGetUp2 = 0x11;   // gets up to standing

// 80102828 (verified in Ghidra, decompiled and assembly), integer distances:
//   band 1: 3000 < d < 0x1389  mask E+35C &= ~0x17 (keeps bit 8)
//   band 2: 0x578 < d < 0x641  mask &= ~0x1B (rearms bit 8)
//   band 3: 1000 < d < 0x579   mask &= ~0x1D (rearms bit 8)
//   band 4: d <= 1000          mask &= ~0x1E (rearms bit 8)
//   otherwise (1600 < d <= 3000, d > 5000): band and bit unchanged.
// Bands 2-4 start other actions (e.g. 0x1C from band 2) that are not ported.
constexpr int kBand1Min = 3000;     // excluded
constexpr int kBand1Max = 5000;     // included (d < 0x1389)
constexpr int kBand2Min = 0x578;    // 1400, excluded
constexpr int kBand2Max = 0x640;    // 1600, included
constexpr int kBand3Min = 1000;     // excluded
constexpr int kBand4Max = 1000;     // included
// 80102AC4: slti v0,v0,0x0C  ->  (rng & 0xFF) < 0x0C
constexpr int kRngThreshold = 0x0C;
// 8010078C: rays at heading +/- 0x28*0x0B angle units (4096 per turn).
constexpr int kSectorHalfAngle = 0x28 * 0x0B;  // 440 -> 38.67 degrees
constexpr int kAngleUnitsPerTurn = 4096;
// 80103C6C: close outcome if the updated distance is < 0x380.
constexpr float kContactDistance = 0x380;
// 80103C6C: the check happens when the Q1 cursor reaches start+8.
constexpr int kExitCheckFrame = 8;

struct StumbleState {
  int band = 0;                  // E+358
  bool attemptConsumed = false;  // E+35C bit 8
};

// 80102828, E+31C == 0: on entering state 4 band and bit are cleared.
void ResetOnChaseStart(StumbleState& s);

// Updates the stored band with the planar distance (8007F4F4 truncates it to int).
void UpdateBand(StumbleState& s, float distance);

// Decision for one update in the chase state.
//   rngByte:        random byte 0..255 (PS1: 80011570() & 0xFF)
//   playerInSector: result of InFrontSector (80100924(E,0x28) -> 8010078C)
// Returns true if the zombie must request the kSelFall sequence.
// As on the PS1, the attempt is consumed even when rng or sector fail.
bool ShouldStartFall(StumbleState& s, int rngByte, bool playerInSector);

// 8010078C: the point (toX,toZ), relative to the zombie, lies in the cone of
// +/- kSectorHalfAngle around the direction (forwardX,forwardZ).
// The MIPS code uses the signs of two cross products with the rotated rays; for a
// half angle < 90 degrees that is the same as the cosine comparison used here.
bool InFrontSector(float forwardX, float forwardZ, float toX, float toZ);

enum class FallExit { Contact, GetUp };

// Outcome at frame kExitCheckFrame of the fall (80103C6C).
//   playerFrontBit: E+320 bit 0x40 (zombie in the sector in front of the player)
//   gateOk:         80100628 (script/player state/other enemies)
FallExit DecideFallExit(float distance, bool playerFrontBit, bool gateOk);

}  // namespace ps1
