# PraxisCompanion GUI bridge

This optional bridge is built into the regular `LHolo.dll`. LHolo retains its
existing projection, placement, DXGI, ImGui frame, WndProc, and input ownership.
No second graphics or window hook is installed for PraxisCompanion.

PraxisCompanion discovers the enabled `LHolo` NativeMod through LeviLamina's
ModManagerRegistry, pins its module, and resolves the five `_v2` C exports.
Registration supplies `IMGUI_VERSION_NUM` and the sizes of `ImGuiIO`,
`ImGuiStyle`, and `ImDrawVert`. LHolo rejects a mismatch before sharing its
ImGui context. The utility hooks remain independent if GUI registration fails.

LHolo's configured shortcuts have priority. Insert opens LHolo's menu and F10
opens PraxisCompanion's menu by default. Both menus use LHolo's existing input
handoff; only one menu can own keyboard, mouse, wheel, and cursor at a time.
Unregister succeeds when the same owner's callbacks were already removed by
provider shutdown and fails when a different owner remains registered.

Provider and consumer unload must each quiesce callbacks before releasing
their DLL. Gameplay and graphics lifecycle require Minecraft runtime testing.
