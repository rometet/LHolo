#pragma once
#include "structure/InventoryCountRules.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/ShortTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace lholo::structure {
using InventoryItemCounts = std::map<std::string, int>;

inline std::optional<int> inventoryNumericTag(CompoundTag const& tag, std::string_view key) {
    if (!tag.contains(key)) return std::nullopt;
    auto const& value = tag.at(key);
    switch (value.index()) {
    case Tag::Byte: return static_cast<unsigned char>(value.get<ByteTag>().data);
    case Tag::Short: return value.get<ShortTag>().data;
    case Tag::Int: return value.get<IntTag>().data;
    default: return std::nullopt;
    }
}

struct InventoryItemsListLookup {
    ListTag const* items{};
    bool malformed{};
};

inline InventoryItemsListLookup findInventoryItemsList(CompoundTag const& tag, int depth = 0) {
    if (depth > 3) return {};
    for (auto const* key : {"Items", "items"}) {
        if (!tag.contains(key)) continue;
        auto const& value = tag.at(key);
        if (value.index() != Tag::List) return {nullptr, true};
        return {&value.get<ListTag>(), false};
    }
    for (auto const& [name, value] : tag) {
        (void)name;
        if (value.index() != Tag::Compound) continue;
        auto nested = findInventoryItemsList(value.get<CompoundTag>(), depth + 1);
        if (nested.items || nested.malformed) return nested;
    }
    return {};
}

inline void addInventoryItemCount(InventoryItemCounts& counts, std::string const& id, int amount) {
    if (id.empty() || amount <= 0) return;
    auto& value = counts[id];
    value = inventoryCountAfterStack(value, amount);
}

inline bool addShulkerInventoryContents(
    InventoryItemCounts& counts, ItemStack const& box
) noexcept {
    try {
        if (!box.mUserData) return true;
        auto const lookup = findInventoryItemsList(*box.mUserData);
        if (lookup.malformed) return false;
        if (!lookup.items) return true;
        if (lookup.items->size() > 27) return false;

        InventoryItemCounts nested;
        ShulkerInventorySlotState slots{};
        for (auto const& wrapped : *lookup.items) {
            if (!wrapped) return false;
            auto const& tag = *wrapped;
            if (tag.getId() != Tag::Compound) return false;
            auto const& entry = static_cast<CompoundTag const&>(tag);
            int const slot = inventoryNumericTag(entry, "Slot").value_or(
                inventoryNumericTag(entry, "slot").value_or(-1)
            );
            int const count = inventoryNumericTag(entry, "Count").value_or(
                inventoryNumericTag(entry, "count").value_or(0)
            );
            if (!claimShulkerInventorySlot(slots, slot, count)) return false;

            ItemStack item = ItemStack::fromTag(entry);
            if (item.isNull()) return false;
            auto const itemId = item.getTypeName();
            if (itemId.empty()) return false;
            addInventoryItemCount(nested, itemId, count);
        }

        for (auto const& [id, amount] : nested) addInventoryItemCount(counts, id, amount);
        return true;
    } catch (...) {
        return false;
    }
}

inline void addInventoryStack(InventoryItemCounts& counts, ItemStack const& stack) {
    if (stack.isNull()) return;
    auto const itemId = stack.getTypeName();
    addInventoryItemCount(counts, itemId, static_cast<int>(stack.mCount));
    if (isShulkerInventoryContainer(itemId)) {
        (void)addShulkerInventoryContents(counts, stack);
    }
}

inline InventoryItemCounts countInventoryItems(Inventory& inventory) {
    InventoryItemCounts counts;
    // Keep LHolo's established bounded 36-slot read. Carried shulker contents
    // are decoded from each stack's already-owned NBT on this same game-tick path.
    auto const slots = std::clamp(inventory.getContainerSize(), 0, 36);
    for (int slot = 0; slot < slots; ++slot)
        addInventoryStack(counts, inventory.getItem(slot));
    return counts;
}
} // namespace lholo::structure
