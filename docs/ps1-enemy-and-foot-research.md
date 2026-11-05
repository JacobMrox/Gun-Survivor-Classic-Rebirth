# PS1 enemy behavior and zombie foot research

Status: investigation only. No AI or skeleton patch is implemented here.

## Reference

[RESPeCt](https://github.com/MachinaAeon/RESPeCt) is a PS1 recompilation.
The inspected revision is `cf880a8d496303ce10f8f08738f320afe01cfe01`.
Its `port/decomp/README.md` describes replacing translated PS1 functions
with readable implementations and comparing behavior in test scenarios.
The repository carries GPL-3.0; this wrapper also carries GPL-3.0. Any
adapted code still requires appropriate attribution, license compliance, and
review of individual files and dependencies.

Useful entry points, using the reference's own names and address space:

| Enemy | Reference | PS1 update |
| --- | --- | --- |
| Hunter | `port/decomp/hunter.c` | `0x8010537C` |
| Cerberus | `port/decomp/cerberus.c` | `0x801031FC` |
| Licker | `port/decomp/licker.c` | `0x80107094` |
| Tyrant | `port/decomp/tyrant.c` | `0x80105270` |

These PS1 addresses and RAM layouts cannot be called or written directly in
the PC executable. Each port needs verified PC entry points, actor and per-slot
state mappings, animation/event mapping, collision and room-script integration,
and tests for lifecycle, frame cadence and multiple actors. Some reference
functions still call translated functions; the presence of a decompiled file
is not evidence that every behavior is independently understood or covered.

## ENEM00 foot layout

The original PC `ENEM00.NAB` examined here has 14 mesh groups, of which the
last is a one-triangle degenerate placeholder. Groups 11 and 12 bind to nodes
13 and 14. In the observed hierarchy both nodes are leaves; there are no
separate child foot nodes. The RE2 zombie source has separate lower-leg and
foot parts, which the current importer combines into those two PC groups. The
shoe geometry is retained, but it follows the lower-leg transform rigidly.

Separating the shoe into another mesh group bound to the same lower-leg node
would allow independent editing/visibility, not independent animation. True
ankle motion requires either adding child nodes and matching animation tracks
to the NAB/NAC pair, or a runtime feature providing equivalent additional
transforms. NAC selector/track compatibility and game code that addresses
parts by index must be investigated and tested before either is safe.

## Proposed sequence

1. Record repeatable PC and PS1 behavior for one enemy in a known room, with
   logs of animation, state, movement, collision and attack events.
2. Map the corresponding PC update, native helpers and actor ownership; verify
   hook signatures against the executable and retain native fallbacks.
3. Port one narrowly scoped behavior with unit tests and gameplay tests,
   including multiple actors, transitions, damage, death and save/reload.
4. Separately prototype extra ankle nodes/tracks on copies of ENEM00, retaining
   a control with the original 15-node skeleton and original NAC.

Do not infer that Hunter, Cerberus, Licker or Tyrant behavior has been fixed
from this research note. No wrapper binary or game asset was deployed.
