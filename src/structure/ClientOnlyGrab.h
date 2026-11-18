#pragma once
namespace lholo::structure::detail {
[[nodiscard]] bool clientOnlyGrabEnabled() noexcept;
// Fixed right-click gesture with a configurable Ctrl/Alt/Shift chord.
[[nodiscard]] int clientOnlyGrabModifiers() noexcept;
void setClientOnlyGrabModifiers(int modifiers);
[[nodiscard]] bool clientOnlyGrabChordPressed() noexcept;
void setClientOnlyGrabEnabled(bool enable);
[[nodiscard]] bool hasLegacyClientOnlyGrabMarker() noexcept;

bool installClientOnlyGrabHooks();
bool uninstallClientOnlyGrabHooks();
}
