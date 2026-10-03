// LHolo - Shared hotkey slot definitions
#pragma once

#include <cstddef>
#include <cstdint>

namespace lholo::input {

// This order is the persistent/runtime slot order. Every hotkey array and
// lookup derives its size or index from this enum so adding a slot cannot
// silently desynchronize input handling, settings, and menu presentation.
enum class HotkeyId : std::uint8_t {
    Gui,
    // Move slots are named by the direction they produce for the player, not
    // by a world axis: the first four follow the player's facing (left/right
    // are perpendicular to it), the last two stay on the world Y axis. The
    // direction itself is resolved in ViewMoveBasis.
    MoveLeft,
    MoveRight,
    MoveForward,
    MoveBackward,
    MoveUp,
    MoveDown,
    LayerIncrease,
    LayerDecrease,
    LoadProjection,
    CloseProjection,
    ToggleManualPlacement,
    OpenPlaced,
    OpenFiles,
    OpenVerification,
    OpenMaterials,
    Count
};

[[nodiscard]] constexpr std::size_t hotkeyIndex(HotkeyId id) noexcept {
    return static_cast<std::size_t>(id);
}

inline constexpr std::size_t kHotkeyCount = hotkeyIndex(HotkeyId::Count);
inline constexpr std::size_t kMoveHotkeyFirst = hotkeyIndex(HotkeyId::MoveLeft);
inline constexpr std::size_t kMoveHotkeyCount
    = hotkeyIndex(HotkeyId::MoveDown) - kMoveHotkeyFirst + 1;
inline constexpr std::size_t kDirectMenuHotkeyFirst = hotkeyIndex(HotkeyId::OpenPlaced);
inline constexpr std::size_t kDirectMenuHotkeyCount = 4;
[[nodiscard]] constexpr bool isDirectMenuHotkey(std::size_t index) noexcept {
    return index >= kDirectMenuHotkeyFirst && index < kHotkeyCount;
}

} // namespace lholo::input
