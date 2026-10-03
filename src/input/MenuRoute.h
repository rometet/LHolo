#pragma once
#include "input/HotkeyTypes.h"
#include <array>
#include <cstdint>
namespace lholo::input {
// Value-only route intent; no ImGui object, engine borrow, or placement action.
enum class MenuRoute : std::uint8_t { None, Placed, Files, Verification, Materials };
struct MenuRouteIntent {
    MenuRoute route{MenuRoute::None};
    std::uint64_t uiGeneration{};
    std::uint64_t worldEpoch{};
    std::uintptr_t window{};
};
inline constexpr std::array<char const*, kDirectMenuHotkeyCount> kDirectMenuSettingKeys{
    "openPlacedHotkey", "openFilesHotkey", "openVerificationHotkey", "openMaterialsHotkey"};
inline constexpr std::array<char const*, kDirectMenuHotkeyCount> kDirectMenuModifierKeys{
    "openPlacedHotkeyModifiers", "openFilesHotkeyModifiers", "openVerificationHotkeyModifiers", "openMaterialsHotkeyModifiers"};
constexpr MenuRoute menuRouteForHotkey(std::size_t index) noexcept {
    if (!isDirectMenuHotkey(index)) return MenuRoute::None;
    return static_cast<MenuRoute>(index - kDirectMenuHotkeyFirst + 1);
}
struct DirectMenuInputContext {
    bool foreground{};
    bool companionVisible{};
    bool lholoVisible{};
    bool gameplayInputEnabled{};
    bool uiInteractionBlocked{};
    bool nativeTextInputBlocked{true};
};
constexpr bool directMenuInputAllowed(DirectMenuInputContext const& context) noexcept {
    return context.foreground && !context.companionVisible && !context.uiInteractionBlocked
        && !context.nativeTextInputBlocked && (context.lholoVisible || context.gameplayInputEnabled);
}
enum class NativeTextInputFlag : unsigned { Focus = 1, Keyboard = 2, Ime = 4 };
} // namespace lholo::input
