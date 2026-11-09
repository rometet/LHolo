# LHolo | English Guide

[日本語 / Project Home](../README.md) · [简体中文](README_ZH_CN.md) · [Releases](https://github.com/rometet/LHolo/releases)

**LHolo** is a client-side schematic projection and building-assistance mod for Minecraft Bedrock Edition on Windows, built on [LeviLamina Client](https://github.com/LiteLDev/LeviLamina). It supports Bedrock `.mcstructure` and Java Litematica `.litematic` files.

> The current public prerelease is **`v26.60.0-port.6`**, intended for **Minecraft Bedrock 1.26.60.05 / LeviLamina Client 26.60.x / Windows x64**. Builds for Minecraft 1.26.51 (the `v26.51.2-ja.x` series) remain available under past release tags.

## Installation

1. Set up the matching Windows Minecraft and LeviLamina Client versions.
2. Download the appropriate Windows x64 client ZIP from [Releases](https://github.com/rometet/LHolo/releases).
3. Close Minecraft, back up any existing `mods/LHolo` folder, then extract the ZIP so that `mods/LHolo/LHolo.dll` exists inside the selected Minecraft instance.
4. Enter a world and press **`Insert`**, or type `LHolo` in chat, to open the menu.

## Features

| Feature | Description |
| --- | --- |
| Schematic projection | Load `.mcstructure` and `.litematic` files |
| Placement & transforms | Move, rotate, mirror, slice layers, and manage multiple placements |
| Block verification | Find missing, wrong-type, wrong-state, and extra blocks |
| Material lists & HUD | Check required materials and building progress |
| Display settings | Adjust opacity, comparison colors, outlines, and layers |
| Experimental helpers | Manual placement, Easy Place, range placement, and material fetching |
| Structure export | Export a selected region as `.mcstructure` |

**Verification colors:** Blue = missing; Red = wrong block type; Yellow = wrong orientation/state; Magenta = extra block.

## Quick start

1. Press **`Insert`** in a world.
2. Use **「ファイル」 (Files)** to browse and load a schematic.
3. Select the placement in **「配置」 (Placements)** and adjust coordinates, rotation, or mirroring in **「変形」 (Transform)**.
4. Use **「検証」 (Verifier)** and **「材料」 (Materials)** to track progress.

| Action | Default shortcut |
| --- | --- |
| Toggle menu | `Insert` |
| Move horizontally | `Ctrl + Arrow keys` |
| Move up/down | `Shift + ↑ / ↓` |
| Change visible layer | `Alt + ↑ / ↓` |
| Move along view direction | `Alt + Mouse wheel` |

Shortcuts can be changed in the menu. Existing saved settings take precedence over these defaults. The fork defaults to Japanese and has a centered menu; other interface languages are available.

## Experimental: Silent Material Grab

Starting with ja.12, you can enable **Silent Material Grab** under the Experimental tab (OFF by default). While a schematic shows missing materials, hold **`Alt` and right-click a chest** containing needed items to attempt a material transfer. No server-side add-on or manual marker file is required. Turn the setting OFF to restore ordinary chest interaction.

Some storage types and servers have not been verified. Test with inexpensive items first and respect server rules when using experimental building helpers.

## Notes

- Disable **Vibrant Visuals** if it interferes with projected rendering.
- The projection itself is client-only. Real block placement and item transfers are still controlled by the world/server.
- Back up important worlds before using prereleases. Screenshots from older LHolo versions may not represent this fork's current UI.

[Changelog](../CHANGELOG.md) · [Source/build information](../SOURCE_BUILD.md) · [License](../LICENSE) · [Third-party notices](../THIRD_PARTY_NOTICES.md)
