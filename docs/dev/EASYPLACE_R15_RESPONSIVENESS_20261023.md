# LHolo Easy Place R15 - Response on aimed ghost

2026-10-23 JST

- Branch: codex/easyplace-responsiveness-r15-20261023
- Based on R14 commit 0172d63. Separate worktree to avoid disrupting current Minecraft or other coding sessions.
- Official target: Minecraft Windows Bedrock 1.26.51.01 / LeviLamina 26.51.5.

## Why

R14 was already installed and verified, but the user reports Easy Place still has noticeable hesitations with the correct block in hand.
Existing user logs show Easy-place enabled while Range placement remains disabled, making the ordinary crosshair path particularly important.

## Changes

1. Easy Place's crosshair and door-upper query uses queryPlacementProjection. The comparison correction cache may still report not-Missing, even though the client world cell has become air. A visible expected ghost plus an actual air cell is now treated as missing *for placement only*. Standard progress/render queries remain unchanged.
2. The native queued rotation validation uses the same placement-focused query to avoid discarding an otherwise valid queued interaction only because the correction scan is late.
3. When Range mode is enabled and the already-held selected material matches an aimed ghost, the safe native placement predictor and transaction are attempted BEFORE the radius list (including projection locking, result collection, and sorting). If focus fails or cannot place, the existing radius scan still runs.
4. No lower server-side safety: 500ms duplicate cell lock, 40ms min interval, 200ms swap retry, 150ms failed general range-plan cache, strict selected-slot/net-id and rotation gates remain.

## Verification

- R14 source contract: 18/18 PASS.
- Production extracted executor fixture: 3194/3194 PASS, including R15 early-focus-scan-skip and normal range fallback assertions.
- Production extracted native rotation delivery fixture: 68/68 PASS.
- R15 source contract: 12/12 PASS.
- Release build: Windows x64 DLL built with clang-cl + xmake and checked to differ from R14; no game/runtime server test performed yet.
- Build priority was reduced to avoid disturbing the ongoing Minecraft session.

## Runtime acceptance

When Minecraft is closed, run R15-Deploy-LocalTest.ps1. The deployment refuses any open Minecraft process, verifies source/target SHA256 and saves reversible R14 backup. Test in a disposable local world first:
1. Turn on Easy Place (Range off). Hold stone, aim at a currently empty projected stone: should attempt on next eligible tick without slot switch, even if the comparison overlay is briefly outdated.
2. Sweep aim across separate adjacent projected full blocks with the correct material selected. No new repeat delay was added.
3. Enable Range with radius 4. Aim at stone while nearby pistons are not placeable; stone should be checked before the complete range scan.
4. With a piston/observer, test exact direction; impossible orientation must not be force-placed. Ensure head/camera remain unchanged.
5. With real blocks already occupying a ghost position, verify no duplicate placement. Respect 500ms cell lock on uncertain sends.
6. Check LHolo HUD, waterlogged projection, and pack reload for no regression.

Report ordinary Easy Place reaction, Range reaction, incorrect block directions, server/restart behavior, and FPS.

## Rollback

Close Minecraft. R15-Rollback-LocalTest.ps1 restores verified R14 DLL and PDB from the deployment backup.
Nothing is installed while Minecraft is running.
