#pragma once
namespace lholo::structure::detail {
[[nodiscard]] bool clientOnlyGrabEnabled() noexcept;
void setClientOnlyGrabEnabled(bool enable);
[[nodiscard]] bool hasLegacyClientOnlyGrabMarker() noexcept;

bool installClientOnlyGrabHooks();
bool uninstallClientOnlyGrabHooks();
}
