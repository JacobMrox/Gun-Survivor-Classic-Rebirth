# Title to New Game transition candidate

## Original behavior and PC fault

PC 00585150 confirms Normal/Easy, keeps the native difficulty flag, resets stats, exits the menu loop and plays GUN.WAV. Its waits at 0058552A and 00585593 then pump window messages for 4779 ms without presenting a new frame. This frozen screen is separate from room loading. The Load Game wait at 00585321 is outside this change.

PS1 TITLE.BIN 8006EE08 enters state 14 and calls 8006FA64: flash counter 24. Drawing 8006C9D8 adds a white quad with clamp((flash-4)*255/18), except the last four flash updates. State 14 handler 8006F180 waits until the flash is zero, then decrements brightness 128 to 0 only on even title updates. 8006FB64 multiplies title sprite colour by brightness/128. This is a multiplicative fade, not the subtractive gameplay fade. The title loop increments 80080630 once per presentation. The previous capture measured 59.82035 title updates/s over a stable menu interval; hardware NTSC cadence is 53693175/(3413*263) Hz.

The sequence takes 279 or 280 VBlanks (about 4.7 seconds), depending on global parity at confirmation. The candidate begins at relative parity zero; it does not claim alignment to the original global counter. Most of the existing PC wait is therefore replaced with visible original animation, not removed as alleged loading time.

## Implementation

Ps1TitleFade is enabled by default. An explicit `Ps1TitleFade=0` in `[SurvivorFix]` keeps the original PC wait for diagnostics. Two byte-guarded wait hooks retain the original game flags, stats and sound calls. The existing Native EndFrame finishes the currently open title scene and captures its image. The existing held-frame renderer presents the original brightness/flash curve, using actual VBlank deadlines so a lower presentation rate samples the same curve rather than slowing it. Native BeginFrame restores the open-scene contract before the original caller resumes. No executable, global clock, room script or gameplay cadence is changed.

The title renderer temporarily excludes other screen effects. It multiplies the destination by the brightness factor, adds the white flash and restores the render state. Its capture is scoped to the transition, avoiding idle title copies and preventing previous-frame effects from leaking into the room. Fixed-function colour quantization can differ from PS1 integer multiplication by one 8-bit output unit.

Unknown ABI, an unavailable initial device or failed initial capture retains the native wait. Default activation follows the observed gameplay acceptance; broader interactive coverage remains open. The PC confirmation audio is preserved; the PS1 layered voices and music-volume fade are outside this visual correction. Device loss, Alt+Tab, Easy mode and repeated start/return sequences still require interactive checks.

## Verification

The curve test first failed on the missing confirmation flash. A second regression first failed when presentation rate affected completion. The PR test suite passes five tests, including 30/60/144 Hz presentation sampling of the original title duration. The protected executable provided matching wait/epilogue signatures and native BeginFrame/EndFrame ABI. A single native Normal-path run on 2026-10-25 observed the confirmation flash and subsequent black. The log recorded 279 updates, 279 presentations and 4665 ms, followed by native movie cleanup and room 07/04 with world and HUD visible. Intermediate fade smoothness was not captured in that automated run; the subsequent user check accepted the effect visually. This does not validate Easy mode, device loss, Alt+Tab, repeated starts, the wake-up trail or full-game parity. The original configuration was restored byte for byte; the title and audio candidates were enabled only for that process.

## User acceptance and remaining startup delay

On 2026-10-25 the user accepted the title transition visually. The subsequent Normal-path run recorded 4664 ms for the effect, then 18656 ms to the room-entry fade callback. Two native movie-completion skip markers occur during that interval. Existing markers do not isolate initialization, cleanup and loading, so the delay is not attributed to a specific operation yet. The next diagnostic scope is those boundaries; the accepted title curve remains unchanged.
