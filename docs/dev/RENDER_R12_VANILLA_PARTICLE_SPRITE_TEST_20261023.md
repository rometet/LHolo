# LHolo R12 — Bedrock vanilla bubble sprite texture integration

Date: 2026-10-23 JST
Branch: codex/vanilla-bubble-native-r12-20261023
Base: R11 020ec95
Worktree: E:/LHolo-Bubble-R12-Native-20261023
Target: Minecraft Bedrock 1.26.51.01 / LeviLamina 26.51.5.

## Evidence from installed 26.51.01 vanilla assets

The on-disk resource archive
`data/resource_packs/vanilla/__brarchive/particles.brarchive`
contains compiled MCB effect resources for
`minecraft:bubble_column_up_particle` and
`minecraft:bubble_column_down_particle`. They refer to
`textures/particle/particles` and a 128x128 sprite sheet.
The face-camera sprite is an 8x8 rectangle beginning at (0,16)
[normalized UV: 0,0.125 -> 0.0625,0.1875]. Their native size
expression is (0.05 * random_1 + 0.1) * (random_2 * 0.9 + 0.2).
The native particle system also includes a kill-if-not-in-fluid
condition for water/flowing_water/bubble_column.

Do not claim that the native particle emitter has been invoked here:
an unmodified emitter cannot persist inside projected water in an
otherwise empty real-world cell, and force-adding water/particles to the
game world would break projection isolation, ON/OFF lifecycle,
multiplayer pack constraints and teardown. A fully identical emitter
would need an isolated virtual-fluid particle context and more GPU
validation, which is **not implemented in R12**.

## What R12 does

- Uses the actual Minecraft texture group and loads the real vanilla
  `textures/particle/particles` asset dynamically on the visible
  projection alpha pass, not from a copied PNG in the mod.
- Draws camera-facing, full-texture 8x8 bubble quads using the extracted
  exact sprite UV and the native two-random size expression.
- Keeps distinct upward/downward motion, seeded variability and fades
  in projected air, with the same bounds (48 visible cells, 56 block
  distance, 4096 cached/8192 scanned) as the accepted R11.
- Fails closed to the user-approved R11 line-ring effect if the native
  particle atlas is missing during an unsupported pack or resource reload.
- Logs `LHoloBubbleR12 nativeParticleAtlas=1 mode=vanilla_bedrock_texture`
  when the native atlas is available, or `nativeParticleAtlas=0
  mode=R11_fallback` if not available.
- Does not add particles, write blocks, change world state, install resource
  packs, hook particle system, or alter other mods or BDS.
- Preserves R5 glass, R8 stairs, R9 slab and R10 trapdoor stable geometry.

## Automatic validation

R12 Release DLL built; R12 pure sprite/UV geometry tests and legacy
R11 CPU animation tests pass. R12 source guards 14 PASS, R11 original
guards 18 PASS. The 12 regression-target summary is generated separately
as R12_TEST_SUMMARY.json. None of these results is a native GPU
acceptance; `visual=NOT_RUN` until the user tests in Minecraft.

## Acceptance test (offline disposable world only)

1. Open Minecraft and the same
   `R9_experimental_slab_trapdoor_bubble.mcstructure` as R11.
2. View the two bubble column blocks: compare shape with R11 wire rings.
   R12 should show the actual Bedrock particle artwork on small,
   directional, camera-facing sprites rather than procedurally
   assembled 8-segment outlines.
3. Test up/down motion, angle changes, camera distances, projection
   ON/OFF, and an in-world resource-pack reload where appropriate.
4. Ensure the accepted R10 cutout trapdoors, R9 slabs, R8 waterlogged
   stairs and R5 color glass haven't regressed.
5. Check latest.log for `LHoloBubbleR12 nativeParticleAtlas=1`.
   If it says 0, the native texture path was not actually used and
   R11 was the visual fallback. Do not call that a vanilla-texture PASS.

Rollback to exact R11 backed-up DLL/PDB after Minecraft exits. Do not
publish as a stable GitHub release before GPU acceptance.
