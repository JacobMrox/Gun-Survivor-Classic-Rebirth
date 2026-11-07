# Decomp references — what to use as base for the PC port fixes

## 1. Survivor PS1 decomp/recomp: RESPeCt (primary oracle for fixes)

Repo: https://github.com/MachinaAeon/RESPeCt — GPL-3.0, unofficial fan PC port of RE Survivor PS1 `SLUS-01087`. **No game data in the repo; bring your own disc.**

Decomp lives in `port/decomp/` (~1,100 readable C functions doing exactly what the original does to RAM, verified RAM-identical over recorded sessions in `tools/eq/`). Main already uses this rev for Cerberus: `port/decomp/cerberus.c` @ `cf880a8d496303ce10f8f08738f320afe01cfe01` — see `docs/cerberus-investigation.md`, `docs/ps1-enemy-and-foot-research.md`, `source/survivor_fix/ps1/cerberus_logic.h`.

Mapping to README's new ambush/enemy batch (all `Not yet`):

| README item | RESPeCt file(s) to read (port/decomp/) |
|---|---|
| Enemy ambush scenes (crows through windows, dogs, Mr. X through wall) | `room_triggers.c`, `script_ops.c`, `script_runner.c`, `script_overlay.c`, `stage*.c` (stage15..32 = room scripts), plus per-enemy below |
| Un-takable items (crowbar room) | `item_pickup.c`, `item_check.c`, `item_combine.c`, `inventory.c`, `field_input.c` |
| Crow flight animation | `crow.c` |
| Mr. X ambushes | `enemy.c`, `enemy_spawn.c`, `stage*.c`, `room_triggers.c` (tyrant slot — confirm PC type id before mapping) |
| Hunters jump obstacles | `hunter.c` (PS1 update `0x8010537C` per `docs/ps1-enemy-and-foot-research.md`) |
| Cleaners/Hank climb down | TBD — not yet mapped (PC type id unknown; `enemy.c` + room scripts to check) |
| Final boss (Hypnos) | `hypnos.c` (per maintainer: final boss, not Cleaners) |
| Already done: Cerberus / dog gait | `cerberus.c` (PS1 update `0x801031FC`); Licker `licker.c` `0x80107094`, Tyrant `0x80105270`, Alligator `alligator.c`, Spider `spider.c`, Moth `moth.c` |

How to use it for the PC port (same rule as before):
1. Keep the decomp outside this repo (e.g. `~/respect-decomp` or alongside `~/<game>-decomp`); gitignore derived data; never ship decompiled source or game files.
2. Do **not** paste translated code in wholesale. Port one narrow behavior at a time into `source/survivor_fix/ps1/*_logic.h` (platform-neutral, unit-testable) + a byte-guarded hook in `source/survivor_fix/` (see `source/survivor_fix/game.h` `CheckBytes`). Retain native behavior for unrecognized enemy types and non-gameplay calls.
3. RESPeCt is GPL-3.0 and this wrapper is GPL-3.0 — adapted code still needs attribution + license compliance, and must keep `tools/recomp/native.txt`-style review. RESPeCt notes: a function is only "done" when `tools/eq/run.sh` shows RAM identical + `tools/eq/coverage.sh` shows it reached; a present `.c` file is not proof every behavior is covered.

Hypnos/tyrant identity check: `docs/ps1-enemy-and-foot-research.md` lists hunter `0x8010537C`, cerberus `0x801031FC`, licker `0x80107094`, tyrant `0x80105270`. PC types must be mapped via `0x00436E40` dispatch + live snapshots (like the type-11 Cerberus mapping) before any RE AI hook.

## 2. RE2 DualShock decomp: bio2-ds (secondary, for engine symbols + third-person)

Repo: https://github.com/MeganGrass/bio2-ds — RE2 DualShock `SLUS_007.48/Leon` disassembly/decomp, educational symbology from the 1997 beta disc `MAIN.SYM` (romaji like "Kage", "Aida"). **No original source / SDK / assets in the repo; PSY-Q required; Make/link files untested.**

Use for:
- **Future: third-person mode** ("using RE2 source code/decomp to port it over" — README). RE2 is the same PS1 engine family (all PS1 RE/Biohazard), so camera/movement/player-control symbols from `Bio2/prog/` are directly portable. Build with PSY-Q per its Makefile, or just read the Ghidra-exported C.
- Cross-platform symbol base for room/item/enemy struct meanings (also feeds `bioclone-remake`, `RE2-Mod-SDK-Ext`).

Not a fix base for Survivor-specific AI — RESPeCt is. Read bio2-ds only when porting RE2-only systems (camera, third-person, config menu).

## Next concrete step (per the skill loop)

Pick one ambush item, map its PS1 entry + PC type id, then port a single behavior with tests — same vertical-slice rule as the zombie/Cerberus fixes. Do not start all six at once.
