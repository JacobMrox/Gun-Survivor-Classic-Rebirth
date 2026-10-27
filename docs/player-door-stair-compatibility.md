# Player doorway and stair compatibility

The PC sphere/mesh response can block an approach accepted by the original PS1 room walls. Door returns could also stay trapped against a native mesh plane after a transition. The adapter now observes the actual collision triangle already read by 00461000, transforms its part/bone chain to world space, and permits only a matching inward horizontal step toward the original 8007C60C allowed side. Native vertical response and progression checks remain authoritative. Invalid, cyclic, translated-unmatched or missing triangle data retains the native result.

Native mode-4 step responders remain available instead of being masked as horizontal obstacles. Optional height support also retains native floor boxes wholly below the player's sphere centre, using the same transformed corners as 0045E980. For the designated player's one query, native height responders are visited before coincident mesh risers; the object table and contact index are restored before returning. The implementation never adds an upward player displacement or enlarges the original doorway range.

Original type-3 scripted approach ranges (80079A28 -> 8008DA00) can be retained along with type-4 door events. These ranges select approach physics; native input, facing, key and progression checks still decide the interaction.

## Configuration

Advanced overrides in `[SurvivorFix]` in `config.ini`:

- `Ps1PlayerHeightSupport=1`: retain and prioritize native height responders.
- `Ps1ScriptedAccess=1`: retain the original type-3 scripted approach ranges.

Both options are enabled by default and follow Ps1PlayerMovement unless explicitly overridden. Broader room and approach-angle coverage remains open. No configuration-window controls are added. The matched inward doorway-return correction and preservation of native mode-4 step responders operate with the existing PS1 player-wall adapter.

## Validation and limits

Production-adapter tests cover the recorded 03/08 return plane, original wall-side direction in several orientations, transformed/cyclic/missing triangle data, disabled contacts, responder ordering and contact-index restoration. The test fixture also retains the earlier doorway and Storage regressions. All seven test targets pass in the combined support patch.

The user traversed 03/08 -> 03/09 -> 03/08 and confirmed the return obstruction was resolved. Earlier native stair trials reached the top of the church steps frontally and at one oblique angle, but other angles still blocked. The latest shower trial was reported as much better; this does not certify every staircase, door, angle or room. PS1 shower measurements retain player Y=0 across that threshold. Native PC vertical compatibility is not a claim that PS1 uses the same height rules.
