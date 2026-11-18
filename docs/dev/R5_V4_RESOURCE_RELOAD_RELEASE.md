# LHolo Glass R5 + Praxis Resource Reload ABI v4

**v26.51.2-ja.9-rc.1** / Minecraft Bedrock Windows **1.26.51.01** / LeviLamina Client **26.51.5**.

This **experimental prerelease** integrates the private-depth-write-disabled Glass R5 projection rendering fixes with a real LHolo resource-reload ABI v4 participant. It is intended to pair with **PraxisCompanion v0.1.3-rc.1 (R33)**.

## Included changes

- Glass R5's private projection blend material and depth-write policy, glass stationary-frame anti-flicker checks, R4 quad sort invariants and liquid proxy occlusion safeguards.
- `lholo_resource_reload_v4` export with real owner/epoch/lease validation; appropriate Pause/Prepare/BeginNative/Finish/Resume barriers.
- Guarded resource-pack mutation callbacks, GPU ownership invalidation/retarget and block icon/renderer cache refresh.
- Existing LHolo UI/Verifier/material/HUD/placement capabilities remain available.

## Runtime results and limitations

The previous standalone Glass R5 branch had a user-reported successful stationary glass test. On **2026-10-21**, the user verified the paired local version in a disposable offline test world after switching one resource pack OFF through Minecraft's standard Global Resources settings: the character remained visible. Praxis recorded Full Stack/UI application complete and both participants successfully resumed their next resource epoch. This demonstrates successful integration for the tested local scenario, **not** general server or normal-world availability.

A compatible Praxis R33 binary is required for this in-world reload workflow. The official Praxis R33 prerelease currently admits **only the specific disposable offline clone** (directory ID PraxisLevelPackOfflineR17, display name Praxis Level RP Offline Test R17), **one ordinary registered local texture pack** at a time. Multiplayer, protected server RP, Level RP mutation, bulk edits, shader/behavior packs and other arbitrary worlds are unsupported. These conditions are enforced in Praxis, not in the entire vanilla Minecraft client.

## Validation

- Windows x64 product compilation/link from public merged source: PASS.
- Native LHolo reload state/owner/leased GPU fixture: 700 checks PASS (D3D11 WARP; game GPU visual pixels not asserted).
- Glass R5 translucency/geometry: 606 checks PASS, water command replay 5,891 checks PASS.
- Logic, UI, graphics, block icons, verifier, materials, NBT and language tests: recorded by clean publication validation.
- Binary release uses the **previously user-tested** integrated Glass R5 + v4 DLL and matching PDB. Independent rebuilding can give different PE/PDB SHA-256; published tested binary hash and exact source tag are included in BUILD_INFO.json.

## Existing issues

- LHolo config save may fail with `replace settings: Access is denied`.
- Native overlay/hook teardown warnings remain on Minecraft shutdown. Avoid dynamic unload/re-enable; restart the game.
- Resource pack toggling outside the designated offline clone is **not approved**.
- No Minecraft game, user worlds, config, private logs, save data or proprietary binaries included in this release.

Install with Minecraft fully closed, back up old LHolo.dll/PDB and config, and replace only the LHolo installation files. Keep the previous DLL/PDB pair as a rollback set. No LHolo setting reset is necessary.
