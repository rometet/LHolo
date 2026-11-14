# LHolo Easy Place: 8×8 mixed-material wall acceptance experiment (unreleased)

## Scope
- Change experimental settings range `easyPlaceColumnDepth` and `easyPlaceMaxPerTick` from 1–4 to 1–8; **keep defaults at 1**, manual/range modes and floor behavior unchanged.
- Native `GameMode::buildBlock` may submit at most eight opaque, state-insensitive full blocks per client tick in a **single visible-top-anchored Y column**, bottom-to-top, with explicit `MobEquipmentPacket` on each material/slot change and **per-hotbar-slot inventory budget**. Existing safety checks for support blocks, orientation, real selected slot, suppressions, and duplicate/pending cells are preserved.
- The eight-height column is only attempted if it is within physical reach; native dispatch success is not proof of BDS acceptance.
- This does **not** place eight different X positions in a tick; eight is the maximum per vertical column.

## Test schematic and environment
- Native Bedrock NBT `tests/fixtures/mcstructure/LHolo_Mixed_Wall_8x8_8perTick.mcstructure`.
- Dimensions X=8, Y=8, Z=1; exactly 64 non-air cells. Palette indices `(x+y)%4`: `cobbled_deepslate`, `polished_blackstone_bricks`, `stone_bricks`, `andesite`.
- Runtime projection anchored at (1,64,4), upper visible Y layer index 7, max=8, depth=8.
- Isolated Bedrock dedicated server `127.0.0.1:19412`; clear X1..8,Y64..71,Z4; solid rear supports Z5, X1..8,Y64..71; a narrow standing platform X1..8,Y65,Z2 for reach.
- At spawn Rome6882 gets four palette blocks in hotbar (64 each), is teleported to X4.5 Y66 Z2.5 facing toward the top of the wall; no unrelated server is restarted.
- BDS meter records `playerPlaceBlock` events, exact expected type, unique cell count, per-tick maximum, and strict per-column ascending Y.

## Checks and release gate
- Source-extracted executor fixture tests eight mixed native builds with seven ordered equipment slot changes; a four-send cap still limits an eight-depth column. Older four-height tests remain.
- Logic tests exercise eight-depth scanning, clamps, and full NBT compressed roundtrip of the native 8×8 structure.
- Production merge is blocked until actual BDS authoritative logs show correct types, no duplicates, and eight placements on the **same server Tick**. If BDS caps lower, document it instead of claiming eight/Tick.
- New DLL is installed only with Minecraft stopped; previous DLL/config are backed up.
