# Survivor Fix integration

These sources are built into Classic Rebirth's `d3d8.dll`. A separate Survivor Fix DLL is not required. `SurvivorFix_Attach` applies the early input-device correction; `SurvivorFix_Start` installs the gameplay hooks after the Classic Rebirth configuration window has saved its settings.

The gameplay corrections are enabled by default and do not add controls to the configuration window. Advanced diagnostic overrides use `[SurvivorFix]` in `config.ini`; see `config.cpp` and `config.h` for the complete list and defaults. The unsupported backward-fall trigger remains disabled. Explicit per-fix overrides still take precedence; the audio corrections follow `Ps1Zombies`, zombie floor support follows `Ps1ZombieChase`, and scripted access/player floor support follow `Ps1PlayerMovement`. Existing configurations without the new keys receive the corrections automatically.

The startup log reports the effective title/audio/access/support options. `Ps1TitleFade` enables the New Game confirmation flash and fade; `Ps1DoorFade` controls door and room-entry fades. They are separate corrections. Their remaining validation limits still apply after enabling them by default.

The current implementation preserves the PS1 logic cadence (about 29.91 updates per second). The FPS limit in the configuration window does not yet decouple rendering from simulation; higher rendering rates are deferred to a later change.

The DirectInput adapter drains the enlarged mouse buffer through the native reader instead of leaving old movement queued. Movie skipping uses the existing `SkipMovies` preference and accepts a new Enter or Space press; native movie cleanup remains on the game thread.

## Regression checks

From the repository root, using an existing C++17 compiler and CMake:

```sh
cmake -S tests -B build/survivor-fix-tests
cmake --build build/survivor-fix-tests --config Release
ctest --test-dir build/survivor-fix-tests -C Release --output-on-failure
```

These tests cover mouse batch draining and movie skip input gating. They do not prove physical mouse latency or visual fidelity. The mouse response was also confirmed in gameplay after installing the integrated build.

The title-to-New Game correction (`Ps1TitleFade`, enabled by default) follows the original PS1 flash and multiplicative fade; see `../../docs/title-new-game-fade.md` for evidence and pending validation. The wake-up camera trail remains incomplete. The room-entry fade in this change is separate from the title transition. Manual movie skipping and the complete Alt+Tab control sequence still need gameplay validation.

The default-on zombie locomotion audio correction and its validation limits are documented in `../../docs/zombie-walk-audio.md`. It uses the original sound bank already present in the PC data; no audio assets are bundled.

The default-on zombie floor-support correction (`Ps1ZombieHeightSupport`) preserves native mesh support after the PS1 planar step. Its measured scope, observed gameplay acceptance and remaining coverage are documented in `../../docs/zombie-floor-support.md`.

Player doorway returns, native stair responders and advanced approach/support options are documented in `../../docs/player-door-stair-compatibility.md`.

The default-on managed-death voice is documented in [zombie-death-audio.md](../../docs/zombie-death-audio.md). Broader PS1 audio comparisons remain open.

Original per-life death-voice bank selection (`Ps1ZombieVoiceBanks`) and the managed death event (`Ps1ZombieDeathAudio`) are enabled by default. See [zombie-voice-banks.md](../../docs/zombie-voice-banks.md) for recognized encounters, original-data validation and remaining gameplay checks.
