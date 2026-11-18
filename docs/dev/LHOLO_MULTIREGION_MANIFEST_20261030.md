# LHolo native multi-region manifest (.lholo)

Implementation: 2026-10-30. Minecraft Bedrock 1.26.51.01 / LeviLamina 26.51.x.

## Format
UTF-8 JSON object saved as `.lholo` in the mod's `schematics` directory:

```json
{
  "format": "lholo-regions",
  "version": 1,
  "regions": [
    {
      "name": "BarteringDecoA-1",
      "file": "BarteringDeco_Parts/BarteringDecoA-1.mcstructure",
      "origin": [1024, 5, -192],
      "size": [48, 40, 48]
    }
  ]
}
```

`origin` and `size` are optional integrity assertions, but recommended. The loader **always uses the actual** `structure_world_origin` and `size` recorded in each source `.mcstructure`. It will reject a missing or invalid origin, duplicate names or duplicate files, and a mismatch with any declared manifest assertions. Names are stable region keys. Relative file names are restricted to validated `.mcstructure` paths within the manifest directory (symlinks resolving outside the directory are rejected).

## Read path and composition
- `.lholo` is included in normal file selection and placement saving.
- `prepareStructureFile` parses all NBT parts on its existing asynchronous preparation thread, and validates their saved coordinates.
- `finalizePreparedStructureFile` resolves native Bedrock palettes via the current client Level and merges each region at `structure_world_origin - globalMinimumOrigin`.
- The resulting structure has a global `sourceWorldOrigin` equal to the minimum corner, and `sourceRegions` plus `regionRenderBlocks` for per-region toggles.
- Non-overlapping regions use a preallocated flat-vector merge and sort. Overlaps use later-part-wins ordering.
- Rendering, EasyPlace, verification, extras and materials use the same effective structure, with disabled regions excluded as uncovered space.
- Composite volume capped at 16 million cells, max dimension 2048, max six million rendered blocks, up to 64 regions. This limits accidental giant memory allocations.

## Bartering Deco test fixture
On the main PC, `schematics/BarteringDeco_25regions.lholo` references `schematics/BarteringDeco_Parts/*.mcstructure`.
Expected region grid: A-1 through A-9, B-1 through B-9, C-1 through C-9.
Actual source files: 29 downloads, 25 unique names; duplicate downloads matched SHA-256.
Absent: **B-5 and C-5**. They are NOT synthesized. The combined region view contains the available 25 files only.
Anchor: X=1024, Y=5, Z=-192; extent: 144×118×144; source-region summed volume: 2,209,536 cells. Native Bedrock blocks and block entities remain native and unconverted.

## Limitations / testing
- `.lholo` is a manifest, not a standalone NBT or litematic file; its component files must accompany it.
- Multiple *regions* can be simultaneously visible within the selected placement; different placements are not currently rendered simultaneously.
- Initial **native** finalization of 25 regions still happens on the main game thread and might cause a temporary stall. The async work covers file I/O / NBT staging only; incremental native finalization is future work.
- Game and BDS acceptance have **not** been verified; test world before changing important builds.
- When source-world origin is clicked, the deployment anchor becomes (1024,5,-192). This may differ from the desired location of the user's own world; placing at player feet also remains possible.
- No GitHub push or release, unless explicitly requested.
