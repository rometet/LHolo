# LHolo R19 — Inventory fast path & experimental native directional EasyPlace

Date: 2026-10-23 JST
Branch: codex/easyplace-slot-and-orientation-r19-20261023
Based on R18 safe-stair commit 0102f47 and includes R17 iron-bar connectivity improvements.
Target Minecraft Windows 1.26.51.01, LeviLamina 26.51.5.

## User report

EasyPlace feels responsive with a matching item already in the hotbar but
hesitates when the item is only in the 36-slot inventory. Stair and trapdoor
placements sometimes have the wrong direction. Requested behavior: face any
direction and let the mod produce the schematic orientation.

## Hotbar / backpack change

The ordinary crosshair EasyPlace path now starts a server-synced inventory
NormalTransaction immediately after finding the correct material in the backpack,
before costly direction planning. It still waits until the next tick / actual
local inventory update to send an item-use transaction. Never place using a
slot whose authoritative contents have not been observed.

It retains 200ms retry backoff on unresolved swaps (to avoid duplicate in-flight
transactions) and prefers the existing selected stack or hotbar match. A
faster RTT or instant delivery is NOT promised.

New five-second counters: swapWait (inventory in-flight backoff) and
hotbarFound (selected/hotbar material is already present). Compare these after
real building to determine whether RTT/backoff or planning is at fault.

## Any-direction native rotation prototype

R18 was fail-closed and refused off-axis stairs. R19 instead searches the
native Block::getPlacementBlock predictor for a yaw/pitch that produces the
exact ghost's controlled states, including stair weirdo_direction /
upside_down_bit and trapdoor direction / upside_down_bit. When matched, one
real native PlayerAuthInput carries BOTH mRot (the player network rotation)
and mInteractRotation set to the same predicted angles, alongside the exact
item-use transaction. The original input fields are immediately restored
after origin(packet) returns; the local camera/head/body actor state is not
changed. An input already owned by the game is never overwritten. Schematic
cell, item/aux/netId, support, reach, and duplicate-cell validation remain.

This is EXPERIMENTAL. Matching the local predictor and packet fields does
not prove remote BDS server acceptance or placement orientation: authoritative
server input, anti-cheat, net delays, and placement timing can differ. Test in a
throwaway local world first and then in an isolated test server. Do not use
this as a correctness guarantee in important player worlds until verified.

## Verification

Production-body extracted executor fixture and native rotation hook fixture
PASS; full 12 regression executables PASS. No server apply/ACK tests yet.
Windows Release LHolo.dll built successfully. The R19 installer has strict
SHA256, active-game refusal, and R17 backup/rollback. Original R17 was still
the installed version at source creation; R18 was staged, never installed.

## Acceptance steps

1. First back up/clone the intended world. Test on an expendable local world
   with EasyPlace ON and Range OFF.
2. Put several schematic stair directions (top/bottom) on a test pad.
   Aim from all four player angles, place each once, inspect the actual block
   direction and upside-down half.
3. Repeat for trapdoors, pistons, observer, dispenser, door and repeater; verify
   wrong-way blocks never remain and no ghost disappears incorrectly.
4. Compare already-in-hotbar vs backpack-only item latency with a full hotbar
   and empty hotbar; note when slot swaps happen and whether repeated swaps
   occur. Inspect EASYPLACE_R16_PERF swapWait/hotbarFound.
5. If incorrect facing, anti-cheat kick, crash, or inventory desync, close
   Minecraft and run R19-Rollback-LocalTest.ps1 to restore verified R17.

No user world, BDS, other mod or resource pack files are modified by build.
