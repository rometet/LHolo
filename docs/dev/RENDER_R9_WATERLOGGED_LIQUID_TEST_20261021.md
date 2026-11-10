# LHolo R9 — waterlogged family expansion and native liquid diagnostics

Date: 2026-10-21 (Japan). Base: accepted R8 7e3ce24.
Branch: codex/waterlogged-extended-r9-20261021.
Target: Bedrock 1.26.51.01 / LeviLamina 26.51.5 / Praxis R33.
Built in a separate local worktree; existing development and worlds are untouched.

## Changes with runtime scope

- R8's confirmed native opaque stair face clipping remains unchanged in
  semantics. R9 additionally classifies opaque slabs (including legacy
  stone_block_slab2/3/4) and only opaque-layer trapdoors.
- Alpha-test/cutout/transparent trapdoors are NEVER clipped based only on
  rectangular native geometry. A texture with a hole cannot be judged
  opaque from its vertex outline. Diagnostic marker records skipped
  trapdoor type and rendering layer (limited to four messages).
- Same-cell native body ownership, exact native water geometry, UV0/UV1,
  staging-then-commit, stream counts and conservative clipping continue.
  Waterlogged lava, liquid-only, glass, pane, fence and bubble geometry
  are not clipped by the extension.
- Dense section lookup now sorts eligible native coverage once per build
  and uses binary search per water quad instead of scanning all body cells.
- Added per-family clipping counters to a new LHoloWaterloggedR9 log marker;
  retained LHoloWaterloggedR8 log marker for regression observability.
- Added LHoloLiquidR9 diagnostic (up to eight section builds) for
  Exact Replay water/lava/bubble secondary-water ownership. This does
  not alter the accepted water/lava sorting or pixel colors.

## Source evidence and test data

R8 was user-confirmed to remove waterlogged quartz-stair flicker on the
specific offline visual fixture. On 2026-10-21 22:39:04 the accepted
R8 log showed water vertices 80 -> 64, 4 quads removed.

Checked-in Chunker mappings contain:
- bubble_column -> bubble_column, waterlogged=true,
  for both drag=true/false, already handled in the Java mapper.
- Common oak_slab and oak_trapdoor waterlogged Java states map to
  Bedrock blocks plus water layer; no guess is needed about mapping values.

Two small projection files were generated, both with full typed NBT
round-trip verification, without touching worlds:
1. R9_native_water_lava.mcstructure: ten exact native water/lava palette
   permutations copied from the existing ice.mcstructure schematic.
   Spatial arrangement is synthetic; block states are captured.
2. R9_experimental_slab_trapdoor_bubble.mcstructure: seven typed
   Bedrock permutations derived from checked-in Chunker mappings and
   native water from ice.mcstructure. Runtime registry resolution,
   block appearance and secondary-water visualization are NOT_RUN.
   This fixture is deliberately experimental, not an assertion of
   current Minecraft native block registry support.

## User offline runtime test

Only in the same disposable offline world; no mandatory server RP or
multiplayer. Keep all resource packs and opacity unchanged for comparisons.

1. R6_quartz-stairs-wet-dry-single.mcstructure: verify R8 flicker fix
   remains (still 15 s / orbit / still 15 s).
2. R9_experimental_slab_trapdoor_bubble.mcstructure: compare waterlogged
   and dry oak slab bottom/top, oak trapdoor closed/open, iron trapdoor.
   If a row is missing or wrong, note which one. Holey trapdoors should
   continue to show their water, not have their water face erased.
   Check the two bubble columns for correct body plus water layer.
3. R9_native_water_lava.mcstructure: confirm water and lava source/flow
   height, tint, side UV, daytime/nighttime lighting and no ghost faces.
4. R6_quartz-stairs-wet-dry-boundary.mcstructure: x=15/16 seam.
5. Bartering.litematic: Glass R5 stationary translucent flicker regression.
6. Open inventory, ShulkerPreview, LHolo and Praxis UI; exit/re-enter world
   once if no unexpected behavior. Do not change resource packs during
   this R9 visual pass.

Find markers in E:/LeviLauncher/versions/1.26.51.01/logs/latest.log:
- LHoloWaterloggedR8 ...
- LHoloWaterloggedR9 clippedStairQuads=... clippedSlabQuads=...
- LHoloWaterloggedR9 trapdoorClippingDeferred=1 ...
- LHoloLiquidR9 exactOwnedCells=... waterCells=... lavaCells=...
- LHoloGlassR5 privateMaterial=1 ...

No marker for a new block family means NO verified clipping of that family.
Native/visual PASS applies only after user confirmation; offline builds,
CPU tests and written fixture validation are not GPU test results.

## Rollback and change boundary

Restore the exact R8 DLL and PDB from R9 backup ONLY AFTER Minecraft exits.
The deployment script must verify pre/post hashes and leave player worlds,
Minecraft settings, Praxis, other mods and server packs untouched. No
automatic GitHub release or protected-world changes.
