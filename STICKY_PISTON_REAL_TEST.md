# LHolo Sticky Piston item-recognition real-device test (2026-11-17)

## Scope
- Minecraft 1.26.60.05 LeviLauncher, client-side LHolo DLL
- Fix: only the exact vanilla `minecraft:sticky_piston` item may satisfy a sticky-piston projection, even if the block-derived combined ID/aux differs from the held item
- All other item families retain their pre-existing ID/aux behavior
- Test schematic: `LHolo_StickyPiston_Hotbar_4x1.litematic` (4 facings; Java properties `extended=false`, `facing` mapped against bundled Chunker data)
- Offline tests prove only mock item lookups/selection and structural checks. They DO NOT prove Bedrock runtime item names, server acceptance or correct orientation.

## Minimal real-world test
1. Open a disposable flat world, not the existing BDS. Put a solid 4-block support floor in place and leave 4 adjacent air cells above.
2. Load `LHolo_StickyPiston_Hotbar_4x1.litematic` from the LHolo schematic folder. Align its four ghosts on empty floor cells.
3. Hotbar: slot 1 ordinary piston or stone, slot 2 sticky piston x4 or more. Select slot 1 (do NOT select sticky piston initially).
4. Easy Place ON, manual/range OFF, preferably max 1 attempt per tick. Aim at the ghost cells. Record whether LHolo automatically switches to sticky piston, whether the item is recognized and whether each placed block has the expected facing.
5. Repeat with manual placement enabled (and other modes OFF), initially selecting the ordinary piston/stone.
6. Negative case: remove sticky piston from entire inventory but keep normal piston. LHolo must refuse the sticky projection; it must never use a plain piston.
7. Check old observer Easy Place remains unaffected.

## Pass/Fail
- No 'missing matching item' with a genuine sticky piston in inventory
- Never places ordinary pistons in place of sticky pistons
- Each placed block has the correct facing; no wrong-cell placement or extra consumption
- Offline fixture/logic tests PASS; Minecraft native/server acceptance is NOT_RUN until user tests.

## Rollback
Completely exit Minecraft, then restore:
`E:\LHolo-Floor-EasyPlace-20261115\backup-before-sticky-piston-20261117-LHolo.dll`
`E:\LHolo-Floor-EasyPlace-20261115\backup-before-sticky-piston-20261117-LHolo.pdb`
to
`E:\LeviLauncher\versions\1.26.60.05\mods\LHolo\`

The always-running BDS and Praxis development processes are outside the change scope.
