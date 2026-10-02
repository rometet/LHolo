#pragma once
#include "structure/InventoryCountRules.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include <algorithm>
#include <map>
#include <string>

namespace lholo::structure {
using InventoryItemCounts = std::map<std::string, int>;

inline void addInventoryStack(InventoryItemCounts& counts, ItemStack const& stack) {
    if (stack.isNull()) return;
    auto add = [&](std::string const& id, int amount) {
        if (id.empty() || amount <= 0) return;
        auto& value = counts[id];
        value = inventoryCountAfterStack(value, amount);
    };
    add(stack.getTypeName(), static_cast<int>(stack.mCount));
}

inline InventoryItemCounts countInventoryItems(Inventory& inventory) {
    InventoryItemCounts counts;
    // Keep LHolo's established 36-slot read; container NBT/shulker decoding
    // has no validated native access boundary in the existing implementation.
    auto const slots = std::clamp(inventory.getContainerSize(), 0, 36);
    for (int slot = 0; slot < slots; ++slot)
        addInventoryStack(counts, inventory.getItem(slot));
    return counts;
}
} // namespace lholo::structure
