# LHolo R11 — animated bubble-column schematic preview

Date: 2026-10-23 (JST)
Branch: `codex/animated-bubble-billboard-r11-20261023`
Base: R10 `e054f5b`
Target: Minecraft Bedrock Windows 1.26.51.01 / LeviLamina 26.51.5
Worktree: `E:\LHolo-Bubble-R11-20261023`

## Goal and implementation

The native bubble_column model is particle-based; Minecraft's
BlockTessellator generates zero body vertices for it. R10 rendered two
static crossed rings in a regular static schematic mesh. R11 replaces
that approximation with up to 4 animated, camera-facing ring sprites
per nearby bubble-column cell.

- Upward/downward motion follows the native `drag_down` state;
  animation has seeded variation in size, phase and horizontal drift
- Cyclic motion fades in/out near the source block boundaries
- The rings face the camera and retain the existing water geometry
  drawn independently by Praxis Exact Replay
- RGB is pale blue/white; native terrain-white UV is used until proper
  vanilla particle atlas binding is available
- Bounded to at most 48 visible cells (6144 vertices), 56-block radius,
  4096 cached bubble cells and 8192 source cells scanned per frame
- Updates on the regular projection alpha pass; hidden schematics do not
  submit this overlay
- Entirely client-only: NO particles spawned into a persistent world
- Separate tessellator, optional-pass exception protection, and
  source generation / rotation / mirror cache invalidation
- Resource-pack epoch remains grounded in the same terrain atlas
- Native bubble_column bodies no longer trigger placeholder cubes

This is a *vanilla-inspired approximation*, not pixel-identical to
Minecraft's particle engine. The vanilla particle texture atlas,
animations, collisions and sound are NOT yet reproduced.

## Regression and source checks

The Release DLL build succeeded. Source-level guards: 18/18 PASS.
The Animated Bubble pure-geometry test completed 495,673 CPU checks.
Eleven runnable regression targets passed, including transparency,
waterlogged clipping, water replay, resource-pack reload, logic,
graphics, NBT, UI, Verifier, materials and icons.

The archived render-contract test depending on an unavailable private
historical source commit is not counted as passing.

GPU visual correctness and animation FPS are NOT_RUN until real
Minecraft acceptance.

## Runtime smoke test (offline disposable world only)

1. Start Minecraft and load
   `R9_experimental_slab_trapdoor_bubble.mcstructure`.
2. View its two bubble-column cells from front, side and above. Up/down
   should move in opposite directions; bubbles should fade and have
   subtle size differences. They should look like small drifting
   outlined particles rather than static crossed rings.
3. Switch projection visibility OFF. No bubble effect should remain.
   Turn ON again and verify the effect resumes.
4. Walk farther than ~56 blocks from the bubble cells: preview bubbles
   should stop rendering, preventing unbounded load.
5. Verify the R10 trapdoor improvement remains, as well as R9 slabs,
   R8 quartz stairs and Glass R5 static color glass.
6. Open inventory, LHolo GUI and Praxis GUI, then leave/rejoin the
   offline world once. Check for missing textures and game crashes.

In `E:\LeviLauncher\versions\1.26.51.01\logs\latest.log`, search:
- `LHoloBubbleR11 animatedBillboards=1 ...`
- `LHoloBubbleR11 cellsVisible=... billboardQuads=...`
- `LHoloBubbleR11 failed ...` (not expected)
- `LHoloBubbleR11 enabled=0 reason=white_concrete_atlas_unavailable`
  (not expected)
- `LHoloWaterloggedR10`, `LHoloWaterloggedR8`, `LHoloGlassR5`

If bubble rings are invisible or incorrect, report screenshot plus
the R11 marker line. User feedback is the source of visual PASS.

## Deployment boundary and rollback

Only the installed LHolo DLL/PDB should be replaced after Minecraft
is stopped. Preserve the exact R10 backup with hashes; never alter
any saves, BDS, resource packs, settings, other mods or active
development processes. Do not publish on GitHub before visual PASS.
