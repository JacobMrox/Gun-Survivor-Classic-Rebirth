# Zombie floor support candidate

The shared PS1 world commits zombie XZ after the PC physics batch. In physical room 03/07, the rendered floor rises from Y=2 to Y=-198, while the PC sphere can remain at root Y=12 after crossing. Its next native sphere sweep begins overlapping the raised surface and does not recover the height. This produces an additional 200 units of floor penetration.

`Ps1ZombieHeightSupport=1` in `[SurvivorFix]` enables an experimental native mesh-support adapter. It is disabled by default and adds no configuration-window control. It requires the existing PS1 zombie world and the verified original PC mesh sweep (0045F090, ECX=object, five stack arguments, RET 14). A signature mismatch keeps it inactive.

After the PS1 XZ commit and pose synchronization, the adapter samples participating native meshes below the sphere centre over the native two-radius probe distance. An upward correction restores the existing sphere-bottom-on-floor invariant. It preserves XZ, animation, velocity and native downward settling. Non-sphere/excluded actors, excluded meshes and registered zombie bodies are not eligible. There is no room-coordinate exception, fixed height, animated-vertex lift or change to PS1 navigation/state logic (8007FEBC -> 8007F404).

This is PC support compatibility, not a new PS1 step-height rule. The original PS1 shower captures retain actor Y=0 across the threshold and the user confirmed no visibly buried feet. Raw PC and PS1 root heights do not establish equivalent visual grounding. The PC native animation path also shows a smaller mesh penetration on the flat floor; this candidate does not correct it or establish PS1 pose parity.

## Validation

The production adapter regression initially failed the recorded raised-floor case and then passed, including option/signature-pointer gates, native participation, actor-body exclusion, finite inputs and preservation of downward settling. All seven local test targets pass. The integrated Win32 build succeeds. Private runtime measurements reproduced the additional 200-unit lift with the candidate enabled; these measurements do not replace gameplay acceptance.

On 2026-10-27, the user reported that the restarted 03/07 trial with living zombies was much better and approved publication. This is acceptance of the observed improvement, not confirmation of every control below. The option remains disabled by default pending broader coverage.

Remaining gameplay coverage, maximum five groups:

1. Living zombies on the lower and raised shower floor; distinguish residual mesh penetration from the corrected floor-level difference.
2. Cross the threshold in both directions without blocking or persistent suspension.
3. Hit, fall, recovery, bite and death without a height jump or altered contact.
4. Shower exit/return and the previously accepted doorway traversal.
5. Ordinary walls and an additional room with different support geometry.

Stop at the first concrete failure and preserve its log. Full-game support coverage and PS1 visual parity remain open.
