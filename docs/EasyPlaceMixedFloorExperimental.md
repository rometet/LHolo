# Easy Place: mixed-material walls and horizontal floors (experimental)

## Behavior
- **Wall / horizontal ray:** aim at a visible upper schematic block. Scan the selected Y-column bottom-to-top for at most 1–4 positions. Hidden layers are queried only through the original vertical-layer gate. Missing inventory materials are skipped; no arbitrary real wall can unlock another column.
- **Floor / vertical ray:** aim down at a visible schematic floor block (upward aim likewise selects a ceiling patch). Treat the aimed block as the anchor of a bounded 2×2 XZ patch when depth=4; 1 disables expansion, 2 and 3 progressively add one neighboring cell. A floor patch is always limited to the **currently visible** schematic layer. The 2×2 orientation follows the sign of the horizontal look vector; if looking straight down, it defaults to +X/+Z. A built anchor must match the schematic before enabling neighboring cells.
- **Mixed inventory:** find the correct Bedrock block-item/aux from hotbar or inventory, including the existing server-synchronized backpack swap. Prioritize the exact aimed floor cell first, then other cells matching the currently selected item. Wait a new tick after a hotbar material change; **never switch item identity/slot within a burst**. Do not mutate client inventory speculatively.
- **Safety:** use the 26.60 native `GameMode::buildBlock` multi-send route only for full opaque, placement-orientation-insensitive blocks with real clicked support. State-sensitive stairs, slabs, walls/fences, multipart items and floating placements keep the prior prediction/legacy path; orientation-sensitive shapes may only dispatch one per tick. Server acceptance always takes priority over client send count.

## Test fixtures
`tests/fixtures/mcstructure/LHolo_Mixed_Wall_4x4.mcstructure` and `LHolo_Mixed_Floor_4x4.mcstructure` are small **synthetic test layouts** using four ordinary material palette entries from a complex Bedrock schematic. These fixtures are **not** the full original schematic. Both are installed separately in the test client's `mods/LHolo/schematics` folder without replacing the user's other files.

## Performance and limitations
The prior homogeneous-stone wall run demonstrated 16/16 actual BDS accepts and 4 accepts/server tick (14 ticks between first and last). **This does not prove an equally fast mixed-material build**. Every item change requires a safe, separate selected-slot update; orientation-sensitive shapes are deliberately slower. Floor patch, mixed-material, complex palette, and inventory reconciliation still require live BDS testing before production merging.

Default: `easyPlaceColumnDepth=1`, `easyPlaceMaxPerTick=1`. Experimental 4/4 must be opted in. Preserve manual/range modes and avoid anti-cheat- or server-rule-violating use.
