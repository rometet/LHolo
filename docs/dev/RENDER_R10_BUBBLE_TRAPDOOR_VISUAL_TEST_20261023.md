# LHolo R10 — bubble glyph and cutout-trapdoor depth separation

Date: 2026-10-23 JST. Base: R9 d0fdcae. Experimental worktree:
E:/LHolo-Render-R10-20261023
Branch: codex/cutout-trapdoor-bubble-r10-20261023
Target: Minecraft Bedrock 1.26.51.01 / LeviLamina Client 26.51.5 /
Praxis Companion R33 compatibility.

## Native findings from the user's October 23 visual run

The R9 experimental 7-cell schematic was loaded successfully.

- Bubble columns: 2 expected bubble_column blocks, both body tessellation
  native output zero vertices. Exact Replay generated 20 water vertices each.
  Root cause for invisible bubble effect is not a missing water layer; the
  engine particle-driven bubble visuals are outside the ghost-block mesh.
- Oak trapdoors and iron trapdoor: native body had 24 vertices/cell
  (RenderlayerAlphatestSingleSide, layer 11). R9 conservatively did not clip.
  Native body plane and water plane overlap in XZ/XY near 0.000999–0.001007
  block separation with significant area (0.18–1.0 square block).
- Slabs: R9 solid clipping was active (9 affected water faces, 140 -> 136
  water vertices), and the user reported no obvious slab flicker.
- Prior R8 quartz stair stationary flicker was visually approved.
- The screenshot also contains a large terrain image / apparent texture
  projection unrelated to this narrow waterlogged-face diagnosis. R10 does
  not modify terrain assets or packs.

## R10 changes and safety limitations

1. For a same-cell ALPHA/cutout trapdoor, derive exact native body quad
   plane(s) and retain water geometry and textures intact. If a water face
   overlaps a trapdoor face within 0.0025 block, shift the entire affected
   water face inward by 0.006 block. No water quad is deleted for cutout
   faces, leaving the trapdoor alpha holes able to show water. Normal source
   water, lava, fence, panes and other waterlogged bodies are unchanged.
2. Preserve R8/R9 full solid-face clipping for opaque stairs and slabs.
   Native water owner ID, color/UV streams and quad metadata remain aligned.
3. Bubble Column: if BlockTessellator generates no bubble body quads, emit
   two static, crossed, two-sided hollow bubble rings per cell as schematic
   geometry, tagged for up/down native drag_down state. Their color uses
   a uniform sample from the white_concrete terrain-atlas tile and is
   tinted bright cyan. The separate native water layer remains drawn by
   Praxis Exact Replay. At most 128 bubble blocks per section use this
   visual helper; allocations include their explicit vertex budget.
   This does NOT simulate vanilla particle animation, and spawns NO real
   world particles or persistent world objects.
4. Resource pack reload, world saves, placements, HUD, menu, inventories,
   and other mods are intentionally unmodified.
5. If no valid white_concrete tile is in the native atlas, the bubble glyph
   safely fails closed and emits a warning rather than sampling invalid UVs.

## Runtime markers

- LHoloBubbleR10 enabled=1 appearance=static_schematic_glyph ...
- LHoloBubbleR10 enabled=0 reason=white_concrete_atlas_unavailable
- LHoloWaterloggedR10 nativeCutoutFaces=... noTextureAlphaCull=1
- LHoloWaterloggedR10 insetCutoutWaterQuads=N insetDistance=0.006
- LHoloWaterloggedR8 ... existing solid clipping telemetry
- LHoloWaterloggedR9 ... slab/stair clipped counts
- LHoloExactR7 ... private material

## User visual acceptance checklist (only disposable offline world)

Load the existing schematic:
R9_experimental_slab_trapdoor_bubble.mcstructure

Test in this order:
1. The two bubble-column blocks (the farthest pair, at local z=12/14):
   there should be visible light-cyan ring marks inside the translucent
   water boxes. Up/down layout is static and differs; it is NOT vanilla
   animated particles. Report whether the marks are visible and if the
   water box continues to shimmer/flicker.
2. The closed/open wooden trapdoors and iron trapdoor. Check head-on,
   diagonally, stationary and rotating. Water should remain visible
   through cutout holes but no longer compete with the solid body face.
3. The bottom/top oak slabs. They should remain visually stable.
4. R6_quartz-stairs-wet-dry-single.mcstructure: R8 stair regression.
5. Bartering.litematic: Glass R5 stationary transparency.
6. Optional R9_native_water_lava.mcstructure: native flow/source colors,
   UVs, and no unrelated visual regression.
7. Open LHolo/Praxis UI, inventory and exit/re-enter world once.

Keep R9 as exact rollback backup. Never replace LHolo DLL/PDB while
Minecraft.Windows is running. No gameplay world, configuration, resource
pack or BDS changes are needed. Built/CPU test PASS != native/GPU visual
PASS. Wait for the user's observation before any GitHub publication.
