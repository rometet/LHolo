# Upstream native bubble-column geometry and shared outline refactor

Reviewed upstream commits:
- `b50ea1927992a542fd67c19ed5ad358709560c5d` (native bubble-column fallback; 2026-10-19)
- `4a25c409a5e48a52478146d58872857b7eff424e` (shared box lines and shader color; 2026-10-19)

## Bubble-column behavior

- Retain the fork's dry/wet layer classification already implemented for `.mcstructure` and `.litematic`; do not synthesize a liquid layer.
- For a bubble-column cell where generic `tessellateInWorld` contributes zero vertices, call Bedrock's `tessellateBubbleColumnInWorld` once as a native mesh fallback.
- Track attempted cells only for the current section using a sparse set. This avoids a full-structure allocation per section build, especially with very large schematics.
- Trust actual produced vertices even if a native tessellator returns false; never draw placeholder cubes when a bubble shape intentionally contributes no geometry.
- Preserve the fork's independent R11/R12 animated bubble sprite overlay and existing vanilla sprite atlas fallback. Its particles and the native mesh are different rendering paths. Visual overlap must be validated in game.

## Shared box outlines

- Adopt upstream pure `boxWireframeVertices`, `buildBoxWireframeMesh` and `ScopedShaderColor`.
- The projection bounds and capture/verifier selection box now use the same 24-vertex white wireframe path with the engine's native selection outline material and per-draw shader color.
- Bounds color changes no longer require rebuilding the GPU mesh when its dimensions stay fixed.
- Exact cell bounds replace the old artificial selection-box +0.01 expansion.
- Keep the fork's depth-test/X-ray switches, mesh primitive modes and colored correction fill/outline geometry; normalize shader multiplier to white and restore its previous value after drawing.
- Preserve the existing liquid replay shader protection by replacing its locally duplicated white-color scope with the shared scope.
- Do not cache live engine material pointers across resource pack reloads.

## Validation

- Release DLL build plus upstream OverlayRenderingTests (225 checks, zero failures).
- Retain the upstream frustum/culling and draw-cache tests, plus the fork's material/HUD, transparency, waterlogged and bubble sprite regressions.
- Visuals and render-thread GPU resource behavior still require actual Minecraft testing before the PR is merged.

## In-game test cases

1. Dry and water-containing bubble columns from both `.mcstructure` and `.litematic`: visible native geometry, animated particles, no full cubes or flickering/double geometry.
2. Selection/capture box and verifier target bounds: exact edges, correct cyan/custom color, no black/white glitches after world/resource pack changes.
3. Missing, wrong-state and wrong-type correction outlines: correct colors, no flicker, depth/X-ray behavior unchanged.
4. Large schematic rotate/walk and camera movement: frame times stay stable, no missing segments, transparent glass and water remain unchanged.
5. Resource pack toggle and world reconnect: colors and draw buffers remain valid.

## Deployment

The test deployment uses `Deploy-UpstreamPerf-R1.ps1` with dated backups; roll back with `Rollback-UpstreamPerf-R1.ps1` while Minecraft is closed. The GitHub main branch and released ja.10-rc.1 are not updated before GPU acceptance.