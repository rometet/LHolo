// Test doubles only: these are not Fake Headers and do not validate Bedrock ABI.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <set>
#include <string>
#include <string_view>

namespace mock {
inline std::set<std::string> itemRegistry{
    "minecraft:stone", "minecraft:dirt", "minecraft:scaffolding", "minecraft:oak_sign",
    "minecraft:redstone", "minecraft:repeater", "minecraft:comparator", "minecraft:furnace",
    "minecraft:redstone_lamp", "minecraft:horn_coral_fan", "test:variant_item"
};
inline int nativeItemConversions{};
}
struct BlockType {
    bool interactive{};
    bool isInteractiveBlock() const { return interactive; }
};
class Block {
public:
    std::string name;
    std::string nativeItem;
    int nativeAux{};
    BlockType type;
    std::string const& getTypeName() const { return name; }
    BlockType const& getBlockType() const { return type; }
};
class MockStack {
public:
    std::string name;
    int aux{};
    std::string worldState;
    bool isNull() const { return name.empty(); }
    std::string getTypeName() const { return name; }
    int getAuxValue() const { return aux; }
    int getMaxStackSize() const { return 64; }
    std::string getHoverName() const { return name; }
    void reinit(std::string_view id, int count, int itemAux) {
        name = count > 0 && mock::itemRegistry.contains(std::string{id}) ? std::string{id} : std::string{};
        aux = name.empty() ? 0 : itemAux;
        worldState.clear();
    }
};
class ItemStack : public MockStack {};
class ItemInstance : public MockStack {
public:
    explicit ItemInstance(Block const& block) {
        ++mock::nativeItemConversions;
        name = block.nativeItem;
        aux = block.nativeAux;
        worldState = "world-only placement/connection states";
    }
};
