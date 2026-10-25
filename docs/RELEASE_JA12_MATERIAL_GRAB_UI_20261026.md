# LHolo v26.51.2-ja.12-rc.1 — Silent Material Grab in Experimental Settings

**Prerelease** for Minecraft Bedrock Windows 1.26.51.01, LeviLamina Client 26.51.5, Windows x64.

## What's new

The Experimental tab now contains a **Silent material grab (Alt + right-click chest)** checkbox. There is no need to create client_only_grab.enabled or install anything on the server.

1. Open LHolo's GUI → Experimental.
2. Tick Silent material grab. If this is your first experimental feature, accept the existing experimental notice.
3. Load a schematic; with missing materials shown in its HUD, hold Alt while right-clicking a chest containing those materials.
4. Turn the checkbox OFF for ordinary chest use without any automatic material grabbing. The state applies immediately, without restarting.
5. Your choice is saved automatically in LHolo's existing settings JSON and restored next time.

For existing users: if the legacy mods/LHolo/config/client_only_grab.enabled marker is present and the new JSON preference hasn't been saved yet, the first launch imports the previous ON setting. The old file is never consulted once JSON has an explicit preference, so OFF reliably overrides it.

## Notes and compatibility

- New installations start with the feature OFF until opted in via the menu.
- ON retains server-authoritative item transfers without creating a chest UI or requiring a server-side add-on.
- For safety, first test with inexpensive items; unusual chest types and servers are not universally verified.
- The earlier invisible native GUI path is disabled along with this toggle; normal chest interaction is unaffected.
- The ja.11 projection optimizations, native bubble-column fallback and shared wireframe outlines remain unchanged.

## Verification

Windows x64 Release build; logic, UI, material grabbing and language/store regressions tested. Matching source and debug symbol archives are provided with SHA-256 checksums.
