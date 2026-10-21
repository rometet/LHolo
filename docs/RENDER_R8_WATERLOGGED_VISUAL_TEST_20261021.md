# LHolo R8 — Waterlogged Stair Face Clipping (2026-10-21)

## Scope and measured input

User reported R6 / R7 waterlogged-quartz-stair projections still looked
overlaid and slightly flickered. R7 confirmed LHoloExactR7 private=1,
sourceDepthWrite=1 effectiveDepthWrite=0. Thus the R7 material change
actually ran and was not sufficient.

The native CPU geometry captures for the test fixture had four opaque
quartz stairs with a water layer. Per cell:
- 48 native body vertices (12 quads).
- 20 native water vertices (5 quads).
- The upper stair solid face and the native water top are separated by
  approximately 0.0009 block, covering approximately 0.5 block squared.
- Side faces are likewise very near opaque stair surfaces on some edges.

This is evidence of surface overlap, not a GPU-pixel causality proof.

## R8 candidate implementation

- Record the native, transformed axis-aligned body quads only for opaque
  "*_stairs" blocks with a secondary liquid in the same projected cell.
- Carry exact structure-index ownership alongside water vertices through
  existing native liquid internal face culling.
- In the Exact Replay CPU stream, before origin conversion and derived color
  generation, clip only water quads near the SAME cell's opaque stair faces.
- Drop fully covered water quads; split partial overlap rectangles into
  surviving fragments while keeping source order, position depth offset,
  texture atlas UV0, optional UV1, colors, liquid kind, owner and quadInfo
  centroid/metadata aligned.
- Unsupported indexed/non-rectangular/nonfinite/extra stream layouts or
  mixed per-vertex color are left unchanged (fail closed).
- Same-liquid internal culling, Glass R5, Exact Replay, R7 private material,
  resource ABI v4, HUD, settings, world data and placement stay unchanged.
- No global block-state/facing guesses are used. No world/GPU calls from
  the CPU clipping helper.

Marker in Minecraft latest.log:
LHoloWaterloggedR8 before=N after=N clippedQuads=N removedQuads=N
resultingFragments=N nativeMaterialUnchanged=1

Marker absence indicates R8 did not clip; it is NOT success.

## Automatic verification (local only)

Windows x64 Release product DLL: PASS.
LHoloWaterloggedClipTests: 38 checks, PASS.
LHoloTranslucencyTests: 606 checks, water replay command 5891, PASS.
WorldPackReload: 700, Graphics: 1638, Logic: 1251291, NBT: 3091,
UI, Verifier, Materials and Block Icon targets PASS.
R8 source contract checks: 14 PASS.
A legacy render-contract target depending on a non-public historical
commit remains NOT_RUN; it is not being mistaken for a passing test.

This candidate's actual GPU visibility, full waterlogged block parity,
frame rate and pixel appearance remain NOT_RUN pending user verification.

## Five-minute user acceptance

Only in the existing disposable OFFLINE test world, not a protected world,
server pack, or multiplayer session.

1. Use R6_quartz-stairs-wet-dry-single.mcstructure at the SAME anchor,
   pack, and opacity as R7. Compare wet stair and dry stair visually.
   Stationary 15s, orbit, stationary 15s. Check no full water cube,
   no flicker, correct stair texture, no missing water in exposed volume.
2. R6_quartz-stairs-wet-dry-boundary.mcstructure: check x=15/16 seam.
3. Bartering.litematic: check glass does not flicker (R5 regression).
4. Open inventory, LHolo and PraxisCompanion GUI and return to game.
5. Verify "LHoloWaterloggedR8 before" appears in latest.log. If absent,
   report that separately: it means no expected clipping occurred.

If broken, exit Minecraft and run the prepared R8 rollback script
to restore R7 DLL/PDB from the exact backup. Do not remove settings.

Do NOT claim all translucent/waterlogged shapes are solved: this is
deliberately limited to opaque stairs and the test fixture.
