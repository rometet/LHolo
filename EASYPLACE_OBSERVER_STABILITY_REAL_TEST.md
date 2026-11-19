# LHolo Easy Place — fast single-input observer test (2026-11-19)

## Behavior / safety
- **NO 2-tick pre-roll for LHolo Easy Place, Manual or Range** (observer, sticky piston, stairs included).
- The existing native planner must first predict the exact required block state.
- The first unowned genuine PlayerAuthInput packet carries item-use plus coherent player/interact/head/camera orientation. No forged additional ticks, direct block-state edits, or server-side bypasses.
- The queued transaction retains exclusive ownership of the selected slot until forwarded/canceled, avoiding duplicate or invalidated attempts.
- **Praxis external requests remain unchanged**; their separately tested conservative pre-roll protocol is not modified.
- In a local integrated server only, the verified rotation is borrowed by one matching native transaction. The authoritative block's runtime state is then compared with the schematic runtime state for diagnosis, without changing any world blocks.

## Test assets
- Installed client: `E:\LeviLauncher\versions\1.26.60.05\mods\LHolo\LHolo.dll`
- Quick fixture: `LHolo_Observer_Hotbar_4x1.litematic`
- 32-block stress fixture: `LHolo_Observer_Stability_4x8.litematic`
- Log: `E:\LeviLauncher\versions\1.26.60.05\mods\LHolo\flexible-placement-trace.log`
- Runtime events: `E:\LeviLauncher\versions\1.26.60.05\logs\latest.log`

## In-game test
1. Enter a disposable local test world; create a 4×8 solid floor and empty air above.
2. In LHolo load `LHolo_Observer_Stability_4x8.litematic` and align its ghosts above that floor.
3. Put stone in one hotbar slot, at least 32 observers in another hotbar slot, and initially **select stone**.
4. Enable normal Easy Place, turn Manual and Range off. Quickly sweep the crosshair across all 32 ghosts.
5. Confirm 32 correct cells and **32 correct facings**, no duplicate consumption and no stalls. Repeat at max per tick = 4; directional blocks still use one transaction per client tick.
6. Test the 4-block fixture from a different initial facing and verify sticky pistons remain distinct from normal pistons.
7. If this is multiplayer BDS, also check remote server world state. Local integrated-server results do not prove acceptance by remote BDS.

## Expected logs
- LHolo directional uses: **`BDS_PLACE ... primes=0`** on first eligible native input; *no `BDS_PRIME`* for LHolo-owned requests.
- In singleplayer integrated-server trace (up to trace limit): `LHOLO_BLOCK_STATE_MATCH` when observed runtime state equals the schematic state; `LHOLO_BLOCK_STATE_MISMATCH` otherwise.
- `LHOLO_ROTATION_DROP` marks a canceled or invalidated pending transaction.
- Offline test success is not proof of real network placement correctness.

## Rollback
Completely exit Minecraft before restoring both original files from:
`E:\LHolo-Floor-EasyPlace-20261115\backup-before-single-authinput-20261119\LHolo.dll`
`E:\LHolo-Floor-EasyPlace-20261115\backup-before-single-authinput-20261119\LHolo.pdb`
to `E:\LeviLauncher\versions\1.26.60.05\mods\LHolo\`.

Do not touch the separate BDS or Praxis processes.
