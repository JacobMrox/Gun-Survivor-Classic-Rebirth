# PS1 zombie death voice banks

`Ps1ZombieVoiceBanks=1` in `[SurvivorFix]`, together with `Ps1ZombieDeathAudio=1`, enables the candidate. Both default to off; there is no configuration-window switch. Walking audio is independent.

## Original rules

`8008DC94` normalizes code resources and binds slots with priority 1 then 0. `8007428C` creates actors in encounter order; `801047B4` stores actor-index parity in the context. `80080A28` loads a high/low pair of voice banks once, using the room's nibbles. Only `F` consumes a random byte, with the original inclusive thresholds and ninth fall-through. `801005BC` addresses resource slot + parity + 3. `800809B4` unloads room banks 2..6; resource keys survive and `80088D4C` releases unused keys.

NOISE entries 27..35 contain eight distinct death recordings: choices 7/8 duplicate the same original bank. Mono cue1 and stereo6/7 share VAG2. Existing PC noise.bin contains every complete bank unchanged. The candidate fingerprints each bank and decodes the original death waveform into a private temporary WAV; no asset is included in the project.

## PC integration

The byte-checked `00430F90` hook brackets native room-enemy construction; the existing `00436B04` initialization notification collects actors. The original native skip byte at `005F2C14` preserves assignments when construction is skipped. Original encounter sections in ROOMENV are parsed for all indexed rooms. The bridge matches the complete native type sequence, uses exact X/-Z positions to resolve conflicting profile metadata, and otherwise retains native audio. Original ordinals retain non-enemy records rather than assuming PC render-object indices are PS1 actor indices.

A voice is assigned once at room construction and kept through the actor's life. Repeated updates, hit reactions, death clips and held corpses draw no new voice. Reused native objects lose old assignments. Dynamic actors created outside a recognized construction and ambiguous/mismatched profiles retain the stock voice. Playback uses the native pool; a failed private-cache load retries the stock path without disabling walking sounds.

## Verified scope

- 12 regression tests pass, including fixtures that compile the production construction bridge and native-load detour. RED/GREEN checks cover weighted selection, RNG consumption, fixed controls, resource reuse, actor parity, skipped construction, lifetime reuse, invalid input and native fallback.
- Optional actual-data checks pass: 102 rooms, 140 encounter profiles and 378 records; nine verified voice choices with eight unique waveforms; altered banks rejected atomically.
- Win32 Release build succeeds. Independent static review found no blocking issue.
- One bounded runtime diagnostic in physical03/07 matched the native two-zombie encounter to its original profile. RNG bytes116/98 selected variants4/3 for ordinals0/1, logical banks4/5. Each actor retained that choice and requested one death voice. Both native non-looping play calls succeeded, all nine cached PCM waveforms matched original hashes, and walking events continued.

The instrumented runtime used synthetic lethal hits through native damage fields. The user subsequently accepted the clean candidate in gameplay after reopening with living zombies. A synchronized real-shot/audio comparison against PS1 remains open. Profile recognition depends on PC construction observables and does not repair existing PC/PS1 spawn or progression differences. Complete encounter coverage, synchronized PS1 RNG state/order, gain, envelope and spatial playback parity are not certified. The room with four zombies in the retained PS1 shower checkpoint is a different encounter profile from the two-zombie PC diagnostic.
