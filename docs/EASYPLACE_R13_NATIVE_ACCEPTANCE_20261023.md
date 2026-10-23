# LHolo R13 — Easy Place directional native-interaction prediction

Date: 2026-10-23 JST. Base: R11 rendering + R12 research branch
`59111b1`, including user-liked animated bubble support.
Development branch:
`codex/easyplace-full-interaction-rotation-20261023`.
Worktree: `E:\LHolo-EasyPlace-Orientation-20261023`.
Target: Minecraft Bedrock for Windows 1.26.51.01, LeviLamina 26.51.5.

## What changed

- Retains exact native `Block::getPlacementBlock` prediction and existing
  strict state matching. It never inserts a guessed raw block state or
  bypasses server authority.
- When unchanged current-pitch/current-yaw placement prediction fails,
  performs a bounded search of alternative interaction rotations.
- Six-facing family: observer, piston, sticky piston, dispenser, dropper,
  barrel. Four horizontal yaw candidates and a vertical pitch candidate
  in each direction (up/down).
- Crafter: up to twelve combinations of horizontal yaw and vertical pitch.
- Horizontal families: trapdoors, stairs, doors, repeaters/comparators and
  other supported placement-orientation families; preserves current pitch.
- Excludes rails and unknown/modded blocks. Hoppers, buttons, torches and
  lightning rods retain native support-face planning rather than guessed
  rotation.
- Only the temporary actor logic `mRot` is scoped inside native
  prediction and always restored. Camera/head/body pitch/yaw are NOT
  rewritten. When prediction matches, a fresh existing native
  PlayerAuthInput carries the exact matched INTERACT pitch/yaw and one
  inventory use. Native movement/head/camera and tick remain unchanged.
- No duplicate interaction packets, no server ack claims, no unconditional
  forced placement. Existing pending-request lifetime, item/net-id,
  structure generation, support block, GUI/input validation remain active.
- Range plan has an additional cap of **four expensive direction candidates
  per tick**, while 16 complex and 64 fast opaque candidates remain
  separate. The range scan can progress past impossible orientation blocks.

## Local verification

- Windows x64 Release LHolo.dll built.
- LHoloLogicTests 1,253,659 checks PASS, no failures.
- Actual production placement/planning fixture (supplied native predictor
  and packet boundary doubles): 3,177 checks PASS, including old trapdoor
  32 cases, six-axis matrix 36, crafter matrix 12, range fairness.
- Actual native input-hook body in offline doubles: 41 checks PASS.
- All eleven R11 rendering/resource/UI/NBT/Graphics/etc regression targets
  PASS on this source tree.
- Source invariants: run `tools/verify_r13_easyplace_contract.py`.
- These tests do not establish Bedrock server acceptance, Minecraft
  placement prediction semantics or state enforcement. Native integrated
  world and multiplayer application are **NOT_RUN**.

## Test fixture

`R13_easyplace_6axis_crafter.mcstructure` uses checked-in Chunker
Bedrock block mapping and the native stone palette/version from the
previous accepted offline schematic. There are 36 six-way blocks
(6 families x 6 directions) and 12 mapped crafter orientations.
Each target block has a stone support cell immediately below.
The same fixture can be used to compare Manual, Easy and Range modes.
The fixture NBT round-trips byte-for-byte and includes a manifest
with each block/state/coordinate. **Native registry resolution and
server placement still require runtime acceptance**.

The private/original source schematics, player world backups, logs,
Minecraft settings, passwords and binaries should not be committed.

## User acceptance checklist — offline disposable world only

1. Load the new `R13_easyplace_6axis_crafter.mcstructure`. Enable
   the projection. First build the indicated stone supports from
   row y=0. Then target y=1 blocks with Easy Place enabled.
2. For observer, piston, sticky piston, dispenser, dropper and barrel,
   try all six facings including up and down. Compare final
   server-observed block orientation to the ghost. Verify a rejected
   direction stays missing instead of placing the wrong block.
3. Rotate your real camera while placing target directions, especially
   toward up/down; camera should not jump to a new yaw or pitch.
4. Try 12 crafter orientation samples. Check their final `orientation`
   state against the ghost; the local fixture uses only exact checked-in
   Bedrock mapping permutations.
5. Re-test ordinary trapdoors (all horizontal orientations and upper/
   lower half), stairs and door base placement. Powered/open bits that
   update after placement should not prevent correct base orientation.
6. In Range, put multiple impossible directional targets near ordinary
   stone targets: normal blocks should still progress with no frame
   stall or repeated send spam.
7. Open GUI/pause, change item and dimension, and switch placement mode.
   No delayed placement should leak through from a queued attempt.
8. Inspect R11 bubbles / R8 waterlogged stair projection visually;
   their source is retained unchanged.

Report for each family: PASS / wrong orientation / not placed /
client crash / HUD mismatch. A screenshot of wrong placement, the
chosen mode, and local latest.log can narrow the next fix.

## Rollback

R13 is a local experimental DLL. Preserve the exact R11 DLL/PDB
(including hashes) in a separate immutable backup before deploying;
only change installed LHolo.dll and LHolo.pdb when Minecraft is stopped.
Do not touch other mods, player saves, BDS, resource packs or configs.
Keep GitHub R6-R12 rendering Draft PR #31 unmerged and independent.
