# Zombie locomotion audio candidate

## Regression

The walk hold introduced in the earlier standalone correction keeps the current selector to prevent repeated animation restarts. Native PC 00436B40 invokes the phase-zero audio dispatcher only when a sequence is reapplied. The zombie handler 0043D550 then plays RICKER/SND-005 for requests 2 through 7. Holding the walk removes most of these events. The integration preserved that dependency; it did not delete the sound files.

A local comparison in room 08/00 retained all other corrections. During the first 300 actor updates, types 0/1/2/4 produced 2/0/1/1 native walking sound calls with the hold enabled and 6/5/8/10 with it disabled. Random choices were not synchronized, so these counts establish the dependency rather than an expected cadence. The diagnostic scene label incorrectly said 07/00; the native room loader and geometry confirmed 08/00.

## Original reference

ENEMY_SLOT40 80105180 selects update lane E+316. Lane 1 calls 80104108, which excludes animation IDs 10,11,14,15,16,17 before reaching 80104660. This routine excludes movement states E+318 0 and 10 and emits only when global counter 80062E10 & 31 is zero. With bit 32 clear and an unchanged integer heading it selects stereo cue 19 / mono cue 10; otherwise stereo cue 17 / mono cue 9. Neither actual displacement nor animation restart is required. Kernel 8001107C increments this counter once per scheduler loop; gameplay uses VSync(2).

Bank 2 comes from NOISE.BIN entry 26. The complete 49152-byte bank is also present unchanged in PC PSX_DATA/noise/noise.bin (SHA-256 2613ecae617dc9eb7b6ac3b6a7094a0ff53832bfef40dfd05fc5bc8017d538d6). Mono cues 9/10 use VAG4/5, centre note 84 at requested note 60. The candidate decodes the existing data into temporary mono PCM WAVs at 11025 Hz. No audio asset is distributed or overwritten.

## Candidate scope and limits

Ps1ZombieWalkAudio is enabled by default and follows the Ps1Zombies group unless explicitly overridden. The adapter covers the existing ported state-4 chase, including turning or blocked locomotion, for all supported zombie model types. It requires the world/navigation/walk corrections and the verified gameplay cadence hook. Its shared stamp advances at the native gameplay wait, not once per actor or render. Startup phase is not aligned with the original PS1 global counter.

The native pooled load/play route remains in use. Only the obsolete zombie walking call is suppressed during a managed gameplay update; other sound callers, hits and falls remain native. The original bank, native ABI and stock route must be available, otherwise replacement stays off. A load failure restores the native path.

This does not implement the remaining original audio update lanes, attack/death voices, distance attenuation or stereo spatialization. Native PC gain and panning remain; no complete audio parity is claimed. A user gameplay check reported the walking sound working well; this does not establish full PS1 audio parity.

## Validation

Unit checks cover the original cue gates, continuous selectors, signed SPU decoding, malformed input, WAV metadata and asset rejection. The C++ decoder output matches the independent PS1 extraction byte for byte for both complete WAVs. A runtime trial in room 08/00 confirmed 16 replacement loads and 16 successful native play calls across zombie types 0,1,2,4. Logged events shared ticks 32,64,96,128,160, with measured gate intervals 1078,1063,1062,1078 ms. Several selectors remained unchanged between successive sound events. The F9 menu stopped actor updates during a three-second read-only observation, but a subsequent automated Enter did not return to gameplay despite the game window being foreground. Interactive input stopped at this first concrete problem; menu return did not pass that run. A later user gameplay check reported the sound working well. Complete visual animation comparison, controlled PS1 listening comparison, hit/fall regression checks and the remaining model variants are still pending; default activation does not establish full PS1 audio parity.
