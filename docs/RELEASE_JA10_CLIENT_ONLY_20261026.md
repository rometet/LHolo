# LHolo v26.51.2-ja.10-rc.1 — Release Notes

Release channel: **prerelease / Windows x64 client**.
Target: Minecraft Bedrock Windows 1.26.51.01, LeviLamina Client 26.51.5.

## Primary feature: client-only schematic material grabbing

- When a schematic is loaded and material HUD identifies missing items, **Alt + right-click** a nearby chest containing those items.
- LHolo uses a standard Bedrock container open + ItemStackRequest + server response lifecycle. No server-side behavior pack, plugin, admin permission or server changes are required.
- The silent path avoids opening the native chest GUI, preventing the temporary HUD/hotbar disappearance that affected the older hidden-screen implementation.
- Source-slot handling matches vanilla `container_items` / `Place`; the client's destination inventory updates only after a matching server-accepted response.
- Client-only right-click detection now supports both local and remote server packets, including delayed ContainerOpen events.
- The user validated material transfer without HUD disappearance in one local world and one external server on 2026-10-26.

## Opt in and install

1. Shut down Minecraft. Back up any existing `mods/LHolo` directory.
2. Extract the ZIP's `LHolo` folder into `LeviLauncher/versions/1.26.51.01/mods/` (use the actual configured Minecraft version directory).
3. To enable the new client-only grab, create an **empty file** named `client_only_grab.enabled` in `mods/LHolo/config/`. Example PowerShell, from the game's version folder:

   ```powershell
   New-Item -ItemType Directory -Force -Path '.\mods\LHolo\config' | Out-Null
   New-Item -ItemType File -Force -Path '.\mods\LHolo\config\client_only_grab.enabled' | Out-Null
   ```

4. Restart Minecraft. With missing schematic materials, press Alt + right-click a chest. Use ordinary unimportant items for the first check.
5. To turn off the experimental client-only path, delete `mods/LHolo/config/client_only_grab.enabled` and restart Minecraft. The older hidden native chest mode remains available.

## Other cumulative changes since ja.9-rc.1

- Easy Place rotation and inventory prefetch/stair correctness, with faster dispatch and selected connectivity behaviors.
- Improved material HUD and verifier HUD consistency.
- Ongoing waterlogged/trapdoor/bubble projection fidelity improvements, including animated bubble sprites.
- Glass R5 and native resource reload ABI v4 remain included.

## Testing and limits

- Material logic regression: 89 checks PASS in the local development environment.
- Windows Release DLL builds with LLVM clang-cl 22.1.8 against LeviLamina Client 26.51.5.
- This is a prerelease, not a guarantee for all Bedrock servers or container configurations. Full inventory, stacked and double chests, shulker variants and unusual packet timings need more testing.
- The client-only material transfer mode must be enabled explicitly using the marker above. It does **not** require the previous experimental BDS-side add-on.
- Known settings-save and hot-unload warnings from earlier prereleases have not been claimed resolved.
- If transfer counts desynchronize, reconnect before another transfer; do not use valuable inventories until validated on your server.

## Verification

The distribution provides a Windows x64 mod ZIP, a separate matching debug-symbols ZIP and SHA-256 checksums. Ensure versions and checksums match the release.
