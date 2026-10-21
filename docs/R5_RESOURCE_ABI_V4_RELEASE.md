# LHolo Glass R5 + native resource reload ABI v4

Prerelease v26.51.2-ja.9-rc.1 | 2026-10-21 JST | Minecraft Bedrock 1.26.51.01 | LeviLamina Client 26.51.5 | Windows x64

## Changes
- Retains Glass R5 private projection blend material with depth writes disabled, original-source transparent quad order preservation, stationary camera safeguards and liquid proxy face occlusion.
- Restores real lholo_resource_reload_v4 export and native Pause, Prepare, BeginNative, Finish, Resume participant receipts with owned GPU/CPU resource reader lifetimes, epoch retargeting and fail-closed invalidation rules.
- Retains LHolo Companion GUI API v2/v3 and HUD control v1. Uses the exact resource-v4 state header tested with Praxis R33, SHA-256 0559e63197779ea6be9ca800e7a73c801abc2a051f3ab71a0ae2616a94a06ce9.
- Includes native state regression tests and fixes Glass R5 liquid proxy and transparency source ordering integration.

## Verified pairing with Praxis R33
Tested local LHolo DLL SHA-256: 192A75B1AEBCC51AA853E9F947862B96EDED1489F3C80DB6C8B04938ABBB1BA0
Tested local PDB SHA-256: D2DA5D2E64A8313E13739AD21E3CF77DB891D2EF91988ED9A2CE4E4055079DAF
Tested Praxis R33 DLL SHA-256: E378263556212E7F08BEC24B5FB3B64A16474D83B664666ECBBD9E8C53CF2786
Praxis release: https://github.com/rometet/praxis/releases/tag/v0.1.3-rc.1

On October 21, 2026, the user tested one resource-pack OFF operation from Minecraft's original Global Resources screen in the disposable offline test world. Native ResourcePack reload finished, both participants returned to their next epoch, and the user confirmed their local player remained visible in third person. The glass R5-only branch had an earlier local visual acceptance, but not every translucent block or server scenario was visually rechecked in this exact combined run.

IMPORTANT: the resource-pack editor, Global stack mutation and player avatar animation/geometry rebind are owned by Praxis R33, not LHolo. This LHolo release supplies the missing compatible v4 participant. There is no independent LHolo-only world resource-pack switch.

## Offline verification
- Native resource state 700 checks PASS.
- Glass R5 translucency checks 606 PASS and water-order CPU tests 5,891 PASS in the local merged candidate.
- Logic 1,251,291 PASS, graphics 1,638 PASS, UI 73,286 PASS, block icon 1,082 PASS, HUD control 139 PASS, language 16 PASS, NBT 3,091 PASS, materials 80 PASS, verifier renderer PASS.
- Release Windows x64 DLL/PDB compiled and linked from the combined source. Public source/product file checks match tested candidate by SHA-256.
- Public test fixture uses deterministic synthetic CPU geometry instead of private captured positions; success of a CPU/mock test does not prove native pixel output.

## Limits and known issues
This prerelease is not validated for normal persistent worlds, multiplayer, mandatory server packs, bulk pack editing or complex shaders/scripts. Praxis R33 admits only a named disposable offline clone and a single ordinary local texture pack toggle. Compatible LHolo alone cannot widen these conditions.

LHolo settings save may log: replace settings: Access is denied. Minecraft shutdown/hot-unload may log overlay hook disable warnings. Neither is fixed here. Avoid hot-unloading native DLLs. Shut down Minecraft before installation or rollback.

The GitHub package preserves GPL-3.0 and dependency source notices. Worlds, user settings, credentials, logs, scripts for a specific computer and private history are not included. No LHolo source or binary is bundled inside the separately released Praxis R33 client ZIP.
