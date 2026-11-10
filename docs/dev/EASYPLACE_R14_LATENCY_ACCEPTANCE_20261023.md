# LHolo Easy Place R14 — placement responsiveness

Date: 2026-10-23 JST
Branch: codex/easyplace-latency-r14-20261023
Base: R13 + R12 native-sprite integrated code 23a4504
Target: Minecraft Bedrock Windows 1.26.51.01 / LeviLamina 26.51.5
Development worktree: E:/LHolo-EasyPlace-Latency-R14-20261023

## User problem

The player holds the correct block item, but Easy Place may take too long
to place the schematic ghost. Improve *placement latency*, not merely
orientation support. Do not reduce server validation or force incorrect
orientations into the world.

## Code path issues confirmed by inspection

1. The 36-slot search chose the first inventory match rather than the
   currently selected valid hotbar stack. Even holding the right item,
   automatic placement could needlessly switch slots.
2. Range mode ignored the already-resolved projected block directly under
   the crosshair, spending the per-tick expensive rotation budget on other
   nearby candidates first.
3. R13's rotation delivery was queued for the next fresh native AuthInput,
   but its caller used a binary sent/failed result. Range mode mistakenly
   marked successful queue admission as failure and could then send a
   second legacy action that invalidated the queued item's selected-slot
   check before native delivery.
4. Manual typematic repeat policy started after 150ms and repeated each
   120ms even though normal game ticks are ~50ms apart.
5. Failed range plans were cached for 250ms even when conditions improved.

## R14 changes

- Prefer the exact currently selected hotbar item, with unchanged
  item+aux equality and official ghost-to-item conversion. The inventory
  search remains the fallback when it is not a valid match.
- Skip redundant Player::setSelectedSlot calls if the chosen stack is
  already held.
- When Range mode is on and the crosshair points to a reachable missing
  ghost within the configured placement radius, try that target first IF
  the matching item is already held. Run the same getPlacementBlock native
  predictor and state/face checks. Respect current-world real occupancy,
  block break suppression and recent-placement lock.
- Distinguish PlacementDispatchResult::Sent / Queued / Rejected.
  A successful rotated transaction queue stops further Range actions
  that tick; only the native packet forwarding path sets send interval,
  recent cell lock, and manual repeat bookkeeping. Queue admission is
  NOT claimed as server acknowledgment or native input delivery.
- The queue function returns bool admission, and a still-live pending
  request retains ownership until the next AuthInput or its timeout.
- Manual held repeat: initial delay 150 -> 100ms, per-placement repeat
  120 -> 50ms, with native AuthInput hook policy kept synchronized.
- Failed range cache: 250 -> 150ms.
- Unchanged: 500ms same-cell send lock, 40ms global send interval,
  200ms rejected-swap retry, reach checks, exact ghost permutation,
  block placement orientation, server-authoritative transaction path,
  native item net ID, budget of four high-cost rotations per Range tick,
  GUI/pause/disconnect and world-session cancellation gates.

## Verification gates

- Native-extracted executor offline fixture PASS (3,192 checks, includes
  15 new R14 latency tests).
- Native-extracted rotation delivery fixture PASS (68 checks).
- R14 source contract 18/18 PASS.
- R12 + R13 Windows Release DLL build.
- 12 runnable LHolo regression test targets (results in R14_TEST_SUMMARY.json).
- No protected world, BDS, player inventory, local settings, or other mod
  files changed during source/build validation.

The checks include: held slot 7 with identical earlier duplicate at slot 2
wins; aimed stone wins first tick over five oriented candidates; successful
queued rotation blocks a competing legacy send; manual first press, 100ms
first repeat and 50ms repeat; cached failed plans retry at 150ms; no
unnecessary selected-slot calls.

Offline doubles do NOT prove actual native server placement/latency. Avoid
claiming a measured FPS or network RTT improvement before visual testing.

## User runtime acceptance (disposable local/offline world)

1. In LHolo load a small schematic with stone plus at least one
   orientation-sensitive block. Switch to Easy Place automatic mode;
   hold the correct stone stack in hotbar slot 7, with another identical
   stack in an earlier slot. Aim at a missing stone and check placement
   starts on the next eligible game tick WITHOUT changing slots.
2. With Range mode on (radius 4), keep correct stone held, aim at stone
   beside several unmatched pistons or observers. It should place the
   aimed stone first rather than stall on complex oriented neighbors.
3. Manual mode: press once to place a single target, then hold right-click
   while moving crosshair along a row of distinct missing blocks. Repeats
   should feel closer to the game's tick rate. Same cell should NOT be
   double-placed while awaiting authoritative server update.
4. Test an observer/piston/dropper/Crafter ghost facing up, down and
   sideways from R13_easyplace_6axis_crafter.mcstructure. Queued
   rotations should place with the correct final direction, or safely
   leave impossible targets missing. Check that camera/head do not jump.
5. Regression: R8 waterlogged stairs, R10 cutout trapdoor, R12 vanilla
   particle-atlas bubble sprite, LHolo menu and inventory switching.
6. Test pause/menu/GUI, dimension transition, and world unload: no
   queued placement should survive to an invalid context.

Report behavior as: first stone delay, manual repeat feels faster/slower,
Range still stalls yes/no, wrong orientation yes/no, crash/FPS problems.

## Rollback

Backup installed R13+R12 pair before replacing LHolo.dll / LHolo.pdb
only while Minecraft is closed. An independent R14 rollback script restores
both exact binaries by SHA256. Never edit other mods or BDS.
PR #31 (rendering R6–R12) remains Draft and separate from Easy Place R14.
