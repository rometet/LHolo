# Client-only material grab experiment — October 25, 2026

Requirement: **no server-side addon, no server modification**, Bedrock 1.26.51 / LeviLamina client only.

## Architecture

Hold Alt and right-click a placed vanilla chest, barrel or shulker while a schematic with missing materials is loaded. The client lets Bedrock send its normal chest interaction request to the server. The unmodified server responds with ContainerOpenPacket and InventoryContentPacket. The experimental client interception captures those packets without creating or pushing a chest UIScene, so the world HUD need not transition. It builds exactly one native ItemStackRequestPacket containing a TakeActionData using the server-supplied container name, source item net ID, and empty player inventory slot; waits for ItemStackResponse and issues ContainerClosePacket. Any rejection/timeout closes the session, never directly grants items client-side or automatically retries.

## Verified

- Source branch feat/material-grab-client-only-protocol-20261025
- Release xmake built successfully against LeviLamina 26.51.5
- Legacy material grab native tests: 88 PASS
- No server addon required: the earlier experimental BDS companion was removed from the isolated scratch world's behavior pack binding.

## UNVERIFIED

- Real server acceptance of TakeActionData in the absence of a native client screen
- Whether normal client inventory actually updates without request registration in ItemStackNetManagerClient
- Zero HUD flash during real play
- Correct server/container close and compatibility with chest, double chest, barrels, colored shulkers, and remote servers

DO NOT treat this build as completed or fully tested. Try ONLY on a test-world copy using dispensable items. The client-only flag is disabled by default.

## Testing & recovery

- Close Minecraft. Run Deploy-ClientOnly-Test.ps1 to back up and install the build. It verifies DLL/PDB hashes and refuses to overwrite while Minecraft is running.
- Run Enable-ClientOnly-Test.ps1 to create mods/LHolo/config/client_only_grab.enabled. This opts in to direct packet handling. No server change is required.
- In a TEST world, load a schematic missing one simple material, put that material in a normal chest, and Alt+right-click. Record whether HUD flickers and whether exact quantities move in both inventories. Inspect LeviLauncher logs/latest.log for MATERIAL_GRAB_CLIENT_ONLY events.
- Run Disable-ClientOnly-Test.ps1 to disable the client-only experiment immediately, returning to previously working native invisible GUI grabbing.
- Close Minecraft and run Rollback-ClientOnly-Test.ps1 if you need to restore the prior DLL/PDB from a timestamped backup.

## Protocol uncertainty

Mojang's server-authoritative inventory protocol validates ItemStackNetId, client request ID, container session and screen-stack request scope. This experiment uses genuine server-provided IDs but may still be denied by unmodified server if the screen-stack protocol requires the client to open its native screen. If denied, avoid bypassing validation; instead reconstruct an invisible request scope that does not push the UI.
