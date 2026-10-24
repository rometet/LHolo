# Material Grabber (Construct-like, Windows 26.51.01)

- Source: `src/structure/MaterialGrabber.cpp`, pure rules: `MaterialGrabRules.h`
- Trigger: **hold Alt (default) while right-clicking an ordinary chest or placed shulker box**. No trigger for normal opening. The vanilla container controller is temporarily used without rendering the GUI; after all requested transfers are confirmed (or a timeout), its screen is closed via LocalPlayer::closeContainerScreen. Normal right-click still opens the normal GUI. The Alt modifier is currently fixed as the initial trigger, not a configurable setting.
- Source of demand: active projection's right-bottom Material HUD requirements (unplaced visible/layer-scoped count). Aggregate requirements sharing the same itemId, subtract the current inventory availability (including the existing carried-shulker count rules).
- Transfer: call the vanilla ContainerScreenController `_handleAutoPlace(amount, collection, slot)` only for an eligible real container collection; one pending request at a time. Require source count decrease AND player inventory count increase before advancing; otherwise stop instead of retrying an ambiguous move.
- Limits: max 108 source slots; no item creation, moving from another dimension, opening closed containers, accessing items inside a stored shulker without placing it, or taking from a disconnected inventory. Full inventories, GUI interference, unavailable HUD, invalid NBT and server rejection fail closed.
- Build/synthetic tests: `LHoloMaterialsTests` (deficit calculations); Release DLL build. Live client inventory authority and chest UI hook invocation **NOT YET VERIFIED**.
- Installation: close Minecraft and run `Deploy-MaterialGrabber-Test.ps1` from this folder. It creates a backup; `Rollback-MaterialGrabber-Test.ps1` restores it after the game is closed.
- Test first in a **copy of a world**: project requires 40 oak logs, carry 25, chest contains 64; Alt+right-click chest should transfer only 15 and return to the world without showing the chest GUI. Then test 2 requested types, no shortages, a full inventory, an empty chest, and a placed shulker box.

## Hidden-GUI handling

The ScreenView::render hook suppresses only the active material-grab session by exact screen-controller identity. It does not suppress other vanilla screens. The native controller still ticks and validates server-authoritative transfer receipts; the screen closes after completion. If a GUI controller never initializes or a close request fails, visibility is restored rather than leaving a stuck hidden menu. This behavior is not yet verified by live play.
