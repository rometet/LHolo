# Selective port: MarmieQi LHolo 26.51.4 projection performance

Upstream commits reviewed:
- `b9b5a368184f3c8be0564f97890549bc95e49d61` — section frustum visibility, build priorities and prefetch
- `afae34c834ff4249b003051324019ef00a66bd0d` — draw-list cache and asynchronous file-load origin safety

Base: rometet/LHolo `e91d9a0` (ja.10-rc.1).

## Implemented in the Praxis-derived renderer

- Adopted upstream 4-plane conservative frustum math and transformed-section AABB math; screen or frustum unavailability conservatively shows all sections.
- Per-camera visibility classification for section build/render, with 16-block prefetch envelope and 2-block render envelope.
- Visible dirty sections are prioritized, with 1 in 8 scheduling selections reserved for background round-robin fairness.
- Section bounds cache is updated incrementally as sections arrive, instead of recomputing prior bounds.
- Adopted upstream `ProjectionDrawCache` value-only CPU draw list cache for terrain buckets, keyed by activation/upload/visibility revisions; no GPU resource pointers are cached.
- Cache invalidation covers async uploads and synchronous fallback rebuilds.
- Frustum culling is applied to terrain meshes, native liquid section submission, block-entity placeholders, and correction fill/outline meshes.
- Existing Praxis glass blending, exact liquid replay and R11 bubble animation are left intact.
- The asynchronous loader preserves the clicked ground anchor in the native-tick value snapshot, making its later activation location independent of player movement. The fork already uses `LoadIntent`/`ProjectionRequestStamp` to suppress stale results and rejects world-epoch changes.
- Added upstream pure culling and draw-cache regression suites plus click-origin snapshot tests.

## Scope and limits

- This is a compatibility **port** of the two high-priority improvements, not a blind cherry-pick or a full sync to upstream 26.51.5.
- The separate upstream bounded biome refresh, rendering fallback removal and experimental state-shared snapshot refactors are not adopted here. They require separate review with the fork's liquid/replay and material contracts.
- No real-world FPS improvement is claimed before Minecraft measurements. CPU unit tests and an offline build do not prove GPU correctness.
- Main branch, latest public GitHub release and BDS server-side files remain unchanged until in-game validation.

## In-game validation

1. With a large schematic loaded, record average FPS and frame spikes at the same location / render distance before and after. Rotate rapidly, move 2-3 chunks away then return; sections must not disappear.
2. Validate distant sections converge, near-camera sections appear first, and no holes persist when stationary.
3. Validate glass, waterlogged slabs, trapdoors and animated bubble columns at daytime and nighttime.
4. Start loading a large schematic, move before completion and verify initial position corresponds to the location when clicking Load (not location when it finishes).
5. Switch world or close the projection while loading: an obsolete result must not reappear.
6. Confirm existing Alt + right-click silent material grab still preserves the HUD and inventory counts.

## Fallback

The test deployment creates a dated backup under `E:\LHolo-UpstreamPerf-R1-20261026\backups`. Close Minecraft and run `Rollback-UpstreamPerf-R1.ps1` to restore the last deployment snapshot.