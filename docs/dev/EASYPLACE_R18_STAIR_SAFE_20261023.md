# LHolo EasyPlace R18 — fail-closed stair facing

Date: 2026-10-23 JST
Branch: codex/easyplace-stair-safe-r18-20261023
Base: R17 commit 687e5d9
Target: Minecraft Bedrock Windows 1.26.51.01 with LeviLamina 26.51.5

## User request / priority

Stairs are sometimes placed facing a different direction from the projected schematic. The user prefers never placing a wrong-facing stair over speculative fast placement. Existing R17 also feels intermittent.

## Source-level problem

R17 compares local Block::getPlacementBlock() against the schematic's
weirdo_direction and upside_down_bit. Yet an exact *local* prediction is not
guaranteed to be the server result. In the immediately-aligned case, R17
transmits an independent legacy InventoryTransactionPacket; it does not
bind that use to the same tick's authoritative player rotation. An off-axis
prediction can temporarily spoof player.mRot and queue an interactionRotation
override while keeping the native packet's player-rotation field unchanged.

Official Bedrock's PlayerAuthInputPacket transmits player rotation,
interaction rotation, client tick, and optional item-use transaction together.
The old split path can be inconsistent for blocks based on player facing.

## R18 changes: prefer skip over wrong-facing placement

1. Stairs require a *real* non-air supporting block. No floating air-mPos
   fallback is allowed. This also prevents ambiguous hit height/half handling.
2. Match BOTH weirdo_direction and upside_down_bit through the native engine
   predictor; no invented rotation candidates or forced facing for stairs.
3. Every successfully predicted stair is **queued** for the next genuine
   PlayerAuthInput packet; no independent legacy stair sender is allowed.
4. At native packet forwarding time, compare BOTH the original player rotation
   and original interaction rotation to the genuine predicted rotation.
   If yaw differs by more than 2 degrees or pitch differs by more than 5
   degrees, drop the stair attempt before transmission. Do not modify player,
   movement, camera, head or interaction rotations for these stair uses.
5. If switching to a different hotbar item, defer stair use to a later game
   tick so the selected slot is not changed immediately before the transaction.
6. Log stairQueued, stairPlanFail and stairSyncDrop in existing five-second
   EasyPlace summary while enabled.

**This is a safety-first, fail-closed change**, not a claim that the user may
face any direction and stairs will be forcibly rotated. Off-axis stairs will
intentionally remain missing; face the direction matching the schematic and
then EasyPlace can submit a native validated interaction. Even this approach
requires real native integrated-server and multiplayer confirmation before
claiming a 100% guarantee of server result.

## Speed and remaining hitches

R17 on 2026-10-23 logged EasyPlace native tick processing mostly within
tenths of a millisecond; the latency was more often missing/invalid target,
recent-cell lock, inventory swap or native/server update rather than
expensive CPU prediction. This R18 update does not remove the 20TPS tick
rate, shorten 500ms duplicate-cell suppression or force extra packets.
Those prevent double-placement of slabs and uncertain-retry duplication.

## Offline verification

- R18 actual extracted production placement fixture, stairs orientations /
  half matrix, real-support/selected-slot safety, and all old fixture cases.
- R18 actual extracted native rotation hook checks both packet angles and
  refuses stale mismatched inputs without cell-lock or send.
- R14-R17 source contracts remain enforced, plus R18 static contract.
- Release LLVM build, existing 12 regression targets, and SHA256 checked
  installer / rollback are required before device deployment.

## User-side runtime acceptance after installing

In a disposable local world, EasyPlace ON, Range OFF:

1. A bottom stair should appear only when its direction matches the currently
   faced target. Repeat with upper stairs and all four compass directions.
2. Turn camera quickly while aiming at stairs: off-axis projections should
   remain missing, not be placed wrong. Confirm head/camera never jump.
3. Aiming directly along the required direction with stone/wood stair selected
   should queue native interaction and place the correct directional variant.
4. Test inventory slot switching just before aiming: it may take one extra
   tick, but must not place wrong-facing stairs.
5. Verify iron bars, glass panes, pistons, trapdoors, normal blocks, materials
   HUD and resource-pack functionality are unaffected.
6. Run at least ~20 seconds. Inspect EASYPLACE_R16_PERF stairQueued,
   stairPlanFail, stairSyncDrop and compare actual placed stairs against ghost.
7. Only deploy with Minecraft CLOSED. The rollback to R17 must restore exact
   SHA256 DLL and PDB if wrong orientation is observed.

No user worlds, BDS, other mods or resource-pack files are modified.
