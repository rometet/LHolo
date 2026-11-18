# Easy Place R13 + R12 native-sprite integration — runtime acceptance

Date: 2026-10-23 JST
Working branch: codex/easyplace-r13-r12native-20261023
Base: de67122 (R12 native Bedrock particle-atlas sprite preview)
Easy Place cherry-pick: 954dc14, integrated as b2bed18
User has accepted the R11 approximation; R12 texture sampling installed for testing
but still awaits separate Minecraft GPU acceptance. Rendering Draft PR #31
is parked and MUST NOT be merged here.

## Combined binary

R13 Easy Place improvements:
- Current rotation predicted first, then candidate pitch/yaw search only
  for known vanilla six-facing and horizontal-orientation families
- Observer, piston, sticky piston, dispenser, dropper and barrel:
  bounded 6-direction native orientation prediction
- Crafter: up to 12 pitch/yaw combinations for native placement prediction
- Existing door/stairs/trapdoor orientation helpers retained
- Same fresh input carries the matched interaction pitch/yaw, while visible
  head/camera/main rotations, native movement and tick remain unchanged
- Strict vanilla getPlacementBlock state comparison before send;
  no raw setBlock, fake ack or unconditional forced placement
- Range mode: four costly rotation plans per tick, while fast opaque plans
  retain their separate budget

R12 native-sprite renderer preserved:
- Uses original Bedrock vanilla particle-atlas sprite UV
- Still uses LHolo bounded schematic-only animation, NOT native particle
  emitter lifetime / collision physics
- No world particle leaks, resource-pack writes or world modifications

## Automated verification

- Windows x64 Release LHolo DLL build succeeded
- 12/12 runnable combined regression targets pass:
  VanillaBubbleSprite, AnimatedBubble, WaterloggedClip,
  Translucency, WorldPackReload, Logic, Graphics, Nbt,
  Ui, VerifierRender, Materials, BlockIcons
- 3,177 production-body offline executor checks pass
- 41 rotation-delivery/hook checks pass
- R13 source contracts: 16/16 pass
- R12 native-sprite contracts: 14/14 pass
- All results stop at test doubles and CPU/GPU build. Native placement,
  exact server authority and cross-resource-pack GPU appearance NOT_RUN.

## Exact experimental schematic

File: R13_easyplace_6axis_crafter.mcstructure
- 36 directed targets: 6 six-facing block families x all 6 facings
- 12 crafter orientation variants
- Exact palette from repository's generated Chunker mappings
- Exact native stone support block from an existing source schematic
- Every test cell has a stone support block immediately below it
- Full typed NBT roundtrip PASS
- Native Minecraft block registry resolution and server application
  must be observed in an expendable OFFLINE test world

## User test

1. Launch Minecraft with the updated LHolo. Load only the disposable test
   world (not active BDS/server, no mandatory server resource pack).
2. Open the new schematic. Place the lower stone support cells first,
   then the observer/piston/sticky piston/dispenser/dropper/barrel blocks.
   Confirm six directions, including both vertical faces.
3. Check the final actual block direction, not only packet delivery.
   If a direction cannot be achieved, Easy Place should safely leave
   the ghost missing rather than placing the wrong permutation.
4. Try all twelve crafter variants, noting missing or wrong orientations.
5. With the player physically looking in a different direction, place a
   desired ghost block. Your camera should not jump; interaction yaw/pitch
   may change only on the outgoing native Input transaction.
6. Check Manual/Range Easy Place modes, including mixed ordinary stone
   and impossible oriented targets. Confirm no significant stutter.
7. Regression: R11/R12 bubble sprite animation, R10 cutout trapdoors,
   R9 waterlogged slab, R8 waterlogged stairs, Glass R5.
8. Open UI, pause, change inventory slot and change world. No old queued
   action should place after the context is invalidated.

Report **per family**: placed with correct direction / not placed /
placed wrong / FPS issue / crash. Screenshot and latest.log for any
wrong orientation help isolate native Bedrock acceptance.

## Deployment / rollback gate

Only replace LHolo.dll / LHolo.pdb after verifying game is NOT running.
Backup the **currently installed R12** DLL/PDB by their SHA256,
without overwriting R11/R12 worktree outputs. Test schematic can be
copied under the existing LHolo/schematics folder. Other mods, world
saves, BDS, resource packs, credentials and config are unchanged.
Rollback script restores the exact R12 pair after Minecraft quits.
