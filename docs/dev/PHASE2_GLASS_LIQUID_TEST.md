# Phase 2 glass / liquid candidate

## Source identity

- Worktree: `E:\LHolo-Phase2-Test-20261016\source`
- Branch: `codex/phase2-glass-liquid-20261016`
- Base: `rometet/LHolo origin/main` `18938b5`
- Target: Minecraft Bedrock `1.26.51.01` / LeviLamina 26.51.x
- Upstream comparison: MarmieQi/LHolo `v26.51.3` (`54660ca`) changelog and public source layout

## Exact changes in this candidate

- Transparent indexed meshes now reject enabled vertex streams whose lengths do
  not match the position domain before a camera-sorted replacement is published.
  This is fail-closed protection for malformed native layouts; valid indexed
  meshes still reorder only their index stream.
- Liquid proxy fallback rejects missing, reversed, zero-area, or non-finite
  still/flowing atlas rectangles instead of submitting zero/invalid UVs.
- Liquid proxy shared-face culling now treats only a same-kind liquid or a full
  opaque block as an occluder. Transparent glass/panes are deliberately not
  treated as opaque, while full opaque neighbors and same-kind liquid layers
  no longer leave proxy ghost faces behind them.
- Preserve the existing, previously approved Java `minecraft:bubble_column`
  conversion as a primary block plus the accompanying water layer. A regression
  check protects this behavior; no bubble-column mapping policy was changed.
- Existing per-section camera re-sort, native liquid UV handling, Praxis Exact
  Replay, GUI/input/HUD, placement, and inventory paths were left intact.

## Automated validation (2026-10-16 JST)

- Windows x64 Release LHolo DLL: PASS with LLVM clang-cl 22.1.8,
  LeviLamina 26.51.5, and existing cached dependencies (no upgrades).
- LHoloTranslucencyTests: 581 checks, 0 failures.
- LHoloLogicTests: 1,182,549 checks, 0 failures.
- LHoloNbtTests: 3,091 checks, 0 failures.
- LHoloLanguageStoreTests: 16 checks, 0 failures.
- LHoloUiTests: 29,427 checks over 4,860 frames, zero ImGui errors.
- LHoloGraphicsTests: 1,638 checks, 0 failures.
- RA01/RA06/RA06Caller additional regression: 17/10/3 checks, all PASS,
  including the retained bubble-column body and water-layer contract.
- Built DLL: source/bin/LHolo/LHolo.dll, SHA-256
  15D8EEB35A0660C8AFB2145C9C128E1282181E1F8C98833824F0FEAB663F50BF.
- Prior installed DLL was backed up before candidate deployment. No in-game
  visual success is inferred from these tests.

## Outstanding issues / runtime required

Static tests cannot prove GPU blend/depth state, exact painter order across
overlapping section boundaries, driver behavior, night lighting, or visual
parity with Praxis. The existing per-section order remains an approximation for
geometry that crosses section boundaries. Praxis Exact Replay remains the
preferred liquid owner; proxy behavior is only a fallback. No Minecraft runtime
PASS is claimed by this document.

## Five-minute Minecraft checks

Use small schematics and record the result as `PASS`, `FAIL`, or `NOT RUN`:

1. Place a glass wall, stained-glass wall, and glass panes spanning a section
   boundary. Orbit around it, look from above and below, and cross the seam;
   check for darkening, flicker, missing panes, or viewpoint-dependent ordering.
2. Load a water source plus flowing-water staircase and a bubble-column tower.
   Check source height, flow height/UV, bubbles as a block model, and no extra
   liquid shell or persistent correction error.
3. Load waterlogged stairs, slabs, fences, and signs beside opaque blocks and
   glass. Check that the body and liquid layers coexist without ghost faces or
   a missing face visible through glass.
4. In the Nether, load a lava pool with a height change and opaque enclosure.
   Check the orange lava appearance, top/side/bottom faces, side UV truncation,
   and no bright/dark seam at section boundaries.
5. With Praxis Companion GUI bridge v3 enabled, open/close the LHolo menu,
   move the mouse, use HUD and inventory, change layers, and leave/re-enter the
   world. Confirm GUI/HUD/mouse coexistence, placement behavior, and no
   inventory change.
