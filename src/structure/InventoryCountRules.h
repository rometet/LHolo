#pragma once
#include <array>
#include <limits>
#include <string_view>

namespace lholo::structure {
inline bool isShulkerInventoryContainer(std::string_view name) noexcept {
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    return name == "shulker_box" || (name.size() > 12 && name.ends_with("_shulker_box"));
}

using ShulkerInventorySlotState = std::array<bool, 27>;

inline bool claimShulkerInventorySlot(
    ShulkerInventorySlotState& slots, int slot, int count
) noexcept {
    if (slot < 0 || slot >= static_cast<int>(slots.size()) || count <= 0 || count > 127
        || slots[static_cast<std::size_t>(slot)]) {
        return false;
    }
    slots[static_cast<std::size_t>(slot)] = true;
    return true;
}

inline int inventoryCountAfterStack(int count, int amount) noexcept {
    if (count < 0) count = 0;
    if (amount <= 0) return count;
    auto const cap = (std::numeric_limits<int>::max)();
    return amount > cap - count ? cap : count + amount;
}
} // namespace lholo::structure
