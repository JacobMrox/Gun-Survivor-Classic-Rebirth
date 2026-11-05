# Cerberus behavior investigation

Status: experimental death-pose adapter implemented; gameplay validation and
the Cerberus AI replacement remain outstanding.

## Reference behavior

RESPeCt `port/decomp/cerberus.c`, revision
`cf880a8d496303ce10f8f08738f320afe01cfe01`, identifies PS1 ENEMPROG 51
and describes a per-dog work block, roaming, circling, steering around walls,
chasing, leaping, biting, damage reactions, and room-specific placement. It
reports RAM-equivalence checks, not PC compatibility. The repo is GPL-3.0;
retain attribution and meet the license terms for any future adaptation.

## PC integration constraints

The wrapper hooks the common enemy update at `0x0043A8A0` from the ordinary
actor loop at `0x0043C86F`. Its existing PS1 zombie navigation explicitly
admits only known zombie types in `ps1/zombie_profile.h`, and the navigation
adapter forwards non-zombies to the original update. This does not establish
which PC type or animation selectors correspond to Cerberus, nor that other
shared hooks are irrelevant. The native mapper has per-type dispatch at
`0x00436E40`. Map and verify the dog identity, lifecycle, update cadence,
animation events, collision and actor state before intercepting its behavior.

## Next measurements

Use the user's save slot 1 without modifying it. Record the PC room, number
and native types of the dogs, and a reproducible symptom. Capture positions,
headings, requested animations and state transitions around the symptom;
compare with the corresponding PS1 behavior and room conditions. Check more
than one actor, attacks and hit/death transitions, and room exit/re-entry.
Preserve native behavior for unrecognized enemy types and non-gameplay calls.

## Slot 1 observation (2026-11-05)

The user's slot 1 loaded PC scene `0x38`, room `03/09`. A read-only live
snapshot found four active enemies, all `S+0x14 == 11`, with distinct native
actor slots 0..3. After the user killed them, a second snapshot found all four
at health 0, native request 15 and selector 4. The user reports that selector
looks like a sleeping animation, and that approach turns in place while the
dog slides toward the player.

The dispatch at `0x00436E40` maps PC type 11 to the case at `0x00437525`.
Requests 15/16 reach `0x00437871`: it initially chooses selector 15, then
after the native completion gate chooses selector 4 and sets `S+0x4C` to 1.
Do not treat a selector number as interchangeable with a PS1 action number:
their bank layouts and transition events must be verified independently.

The PS1 reference's dog death handling selects animation 0x0F on lethal hit;
after its knockback clip ends it stops its animated parts in the lying pose.
It also has explicit per-actor circling, wall avoidance, chase and leap state.
Capturing the native clip-15 completion and mapping the per-track cursor rules
is needed before replacing the PC's selector-4 transition; avoiding that
transition without holding the completed tracks can cause the fall to loop.
Keep kill counting, collision and room-script effects intact.

A complete PS1 AI replacement requires the remaining PC animation, damage,
collision and room-state mappings, plus lifecycle and multi-dog validation.

## Initial trace and isolated logic

A 60-second read-only PC trace recorded two live dogs moving substantial
distances while requests 5/6 and selectors 11/10 were active, with frequent
switches to movement selectors. This is consistent with the reported sliding,
but does not by itself identify which native movement operation is responsible.
No new death occurred during that trace.

`ps1/cerberus_logic.h` now contains an isolated, partial model of the PS1
roam-alert, chase-branch and leap-gating decisions. Its boundary and per-actor
wander-state tests pass. The code is intentionally **not wired to the game**:
PC distance/coordinate conversion, wall tests, animation mapping, attack and
death events, actor lifetime, and per-frame ordering still need verification.
It must not be described as a complete port or a gameplay fix.

## Candidate death-pose adapter

`Ps1CerberusDeath` defaults on and can be disabled through `[SurvivorFix]`.
For PC type 11, the mapper is still called so its native side effects occur;
when its death-fall selector 15 would change to selector 4, the adapter keeps
15 only if it has valid tracked animation state. It uses the existing
per-track completion/hold mechanism, and restores finished tracks after draw.
The feature requires the byte-verified existing DrawModel hook; otherwise it
falls back to the native mapper. It resets with native actor/room lifetime.

The native Win32 build and isolated selector-gate tests pass. This is an
experimental targeted correction, not a Cerberus AI replacement. It still
requires tests for fall pose, kill counting, blood pool, off-screen corpses,
multiple dogs, pause, scene changes and save/reload before acceptance.
