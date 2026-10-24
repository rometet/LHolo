# Material Grabber (Construct-like, Windows 26.51.01)

- Source: `src/structure/MaterialGrabber.cpp`, pure rules: `MaterialGrabRules.h`
- Trigger: **hold Ctrl while right-clicking to open an ordinary chest or placed shulker box**. No trigger for normal opening. The chest UI remains available; there is no world or inventory data modification outside the official controller.
- Source of demand: active projection's right-bottom Material HUD requirements (unplaced visible/layer-scoped count). Aggregate requirements sharing the same itemId, subtract the current inventory availability (including the existing carried-shulker count rules).
- Transfer: call the vanilla ContainerScreenController `_handleAutoPlace(amount, collection, slot)` only for an eligible real container collection; one pending request at a time. Require source count decrease AND player inventory count increase before advancing; otherwise stop instead of retrying an ambiguous move.
- Limits: max 108 source slots; no item creation, moving from another dimension, opening closed containers, accessing items inside a stored shulker without placing it, or taking from a disconnected inventory. Full inventories, GUI interference, unavailable HUD, invalid NBT and server rejection fail closed.
- Build/synthetic tests: `LHoloMaterialsTests` (deficit calculations); Release DLL build. Live client inventory authority and chest UI hook invocation **NOT YET VERIFIED**.
- Installation: close Minecraft and run `Deploy-MaterialGrabber-Test.ps1` from this folder. It creates a backup; `Rollback-MaterialGrabber-Test.ps1` restores it after the game is closed.
- Test first in a **copy of a world**: project requires 40 oak logs, carry 25, chest contains 64; Ctrl+open chest should transfer only 15. Then test 2 requested types, no shortages, a full inventory, an empty chest, and a placed shulker box.
