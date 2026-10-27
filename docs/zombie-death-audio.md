# Managed zombie death voice

## Observed cause

The PS1 lethal-hit path `80102224` requests `801005BC(E, 6, 1)` before selecting its death animation. This is a resource-specific voice bank; it is not shared locomotion bank 2.

The native PC `0043D550` requests family 0/cue 1 only for selectors `12/13` under death requests `0F/10`. The PS1 death adapter can use `0B -> 0C` without either selector. It also sets `S+5C` on grounding before optional `12/13` follow-ups; native `00436B40` excludes animation audio once that field is nonzero.

A bounded native-damage diagnostic confirmed health reached zero, managed clips `0B -> 0C` completed, and the pool was created. Only hit cue 0 was requested; death cue 1 was absent. The other zombie continued producing walking events. This diagnostic injected one lethal hit through the native damage fields; it is not a real-shot gameplay acceptance test.

## Candidate

`Ps1ZombieDeathAudio=1` in `[SurvivorFix]` opts in. The default is off, with no configuration-window control. The candidate requests the shipped PC death voice through native pooled playback once on managed death entry, independently of the walking-audio option. It checks the original sound-entry bytes before installation. Unsupported models, living actors, existing pools, missing clips and native fallback retain existing audio ownership. Held death clips and optional ground follow-ups do not request another voice. Actor reset permits a fresh event for a new life.

The death plan, random selection, animation speeds, hit voices and walking sounds are unchanged. A missing sound backend does not prevent the existing death animation.

## Validation status

- Regression fixture compiles the production death mapper and voice adapter. It covers all nine shared PC zombie types, head/body and grounded plans, repeated mapping, separate actors, actor reset, disabled/unavailable paths and native fallback.
- All eight regression tests pass, and the Win32 Release candidate builds. A static review found no blocking issue.
- A second native-damage diagnostic requested family 0/cue 1 exactly once, loaded stock `ZOMBIE1/SND-001.WAV` successfully, and the native backend accepted non-looping playback. The managed death settled, while the other actor continued producing walking events.
- The user accepted the clean candidate in gameplay after reopening with living zombies. A synchronized real-shot/listening comparison against PS1 remains pending. The first diagnostic required manual closure after normal close commands failed; no forced termination was used.
- Exact PS1/PC voice sample identity, resource-specific variation, gain and spatialization are not established by the event comparison. No full-game audio-parity claim is made.

## PS1 voice variety verified from original data

The original loader `80080A28` selects voice banks using the two nibbles of its room-data parameter. A nibble of `F` requests the existing weighted random selection: eight cumulative thresholds permit nine outcomes (`0..8`). With the original zombie base at `800A22BC`, these outcomes load `NOISE.BIN` entries `27..35`. This selection happens when loading the banks; it is not a new random choice in the lethal-hit voice wrapper.

In all nine entries, death mono cue `1` and stereo cues `6/7` reference the same VAG sample `2`. Decoding the original SPU data gives **eight distinct death waveforms**: selectors `7` and `8` have identical banks and samples. Thus the PS1 does not have a single universal zombie death recording. A zombie still uses the voice bank addressed by `E+84E + context[90] + 3` at the time of the event.

The preserved shower checkpoint has four active zombies alternating logical banks `4/5` by their own actor-index parity; both banks have loaded source entry `27`. Hearing the same death voice from that group would therefore be consistent with that checkpoint; it does not establish a universal recording for the whole game.

Every one of the nine complete original banks has an exact byte match in the already-installed PC `PSX_DATA/noise/noise.bin`. No new sound assets need to be distributed to reproduce this inventory. The first candidate uses only the native PC death cue, observed loading `ZOMBIE1/SND-001.WAV`. The additional opt-in `Ps1ZombieVoiceBanks` candidate now implements original bank selection and per-life assignment for recognized native encounters; see [zombie-voice-banks.md](zombie-voice-banks.md).

This verification was offline: existing PSX exports, the preserved checkpoint, original disc sectors, SPU decoding and exact hashes. It did not start another game, alter the open PC session, change binaries or claim acoustic/spatial playback parity. The bank-selection follow-up restores the verified rules rather than choosing a random sound independently on each death. Its tested scope and recognition limits are documented separately.
