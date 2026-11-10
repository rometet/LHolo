# LHolo R7 - Exact Replay depth-write isolation (2026-10-21)

## User visual feedback and log evidence

The user tested R6_quartz-stairs-wet-dry-single.mcstructure and reported
partial flicker / visually overlapping projection surfaces, not rapid glass flicker.
The screenshot also shows a large background texture anomaly; that is a
separate symptom with no confirmed cause.

The 2026-10-21 18:59:39 and 19:00:40 LHolo logs show:
- LHoloGlassR5 privateMaterial=1, baseDepthWrite=1, privateDepthWrite=0.
- Four waterlogged quartz-stair body cells produced 192 body vertices.
- Praxis Exact Replay: 4/4 positive water cells and 80 water vertices,
  no UV failure or double-liquid build, no retained fallback.
- LHoloLiquidR6 diagnostic did not trigger: R6 proxy fallback was not used.
- Body upper-step and water top have overlapping x-z area 0.5 block squared
  per stair and vertical plane difference 0.000900 block in both capture batches.
This suggests depth/transparency competition but is not proof of GPU z-fighting.

## Conservative R7 experiment

R5 used a projection-owned copy of the vanilla mMatBlendBlock material with
depthWriteMask=0 for regular translucent ghosts. R6 extended it to proxy
liquid and placeholder fallback meshes. Exact Replay water continued using
vanilla mMatBlendBlock (depthWriteMask=1), drawn before ghost body meshes.

R7 resolves the R5 private blend material before Exact Replay and shares it
among projected Exact Replay water, retained-native blend fallback, regular
ghost meshes and R6 proxy/placeholder. No mesh vertices, UV, colors, opacity,
sort, face culling, texture, world state, resource ABI, GUI or placement are
intentionally modified. The alternate SignText diagnostic remains unchanged.
If R5 private material creation fails, its vanilla fallback remains.

Runtime marker:
LHoloExactR7 path=PraxisExactReplay sections=... private=1
sourceDepthWrite=1 effectiveDepthWrite=0 effectiveDepthTest=1 blend=1

If private=0, the intended R7 material was not selected.

## User acceptance in a disposable offline test world

1. Load the SAME R6_quartz-stairs-wet-dry-single.mcstructure, same position,
   opacity, camera angle, pack as the R6 screenshot. Compare wet and dry
   bodies for slight flicker, occlusion, missing faces and water coverage.
   Hold still 15s, orbit, then still 15s.
2. Inspect R6_quartz-stairs-wet-dry-boundary.mcstructure across x=15/16.
3. Retest Bartering.litematic for Glass R5 stationary flicker.
4. Open inventory and LHolo/Praxis GUI for rendering regression.
5. Compare the background texture anomaly with LHolo projection visible
   versus hidden, without changing resource packs or worlds.

Use latest.log to check LHoloExactR7. GPU appearance, water geometry and the
unrelated background anomaly are NOT_VERIFIED until runtime visual results.

Backups: R5 backup-before-r6-20261021, R6 backup-before-r7-20261021.
Independent dev tree: E:/LHolo-Render-Fix-20261021.
Do not publish stable or change DLLs while Minecraft is running.
Known separate issues remain: config.json Access denied, hook teardown.
