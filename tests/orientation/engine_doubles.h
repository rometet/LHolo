// Explicit engine doubles. These do NOT emulate Minecraft placement or NBT ABI.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <charconv>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include "block/BlockOrientationRules.h"
#include "place/ManualPlacementRules.h"

using States = std::map<std::string, std::string>;
struct CompoundTag {
    States mTags;
    auto begin() const { return mTags.begin(); }
    auto end() const { return mTags.end(); }
};
template<class T> struct StateKey { using Type = T; char const* key; };
namespace VanillaStates {
inline StateKey<bool> UpperBlockBit() { return {"upper_block_bit"}; }
inline StateKey<bool> DoorHingeBit() { return {"door_hinge_bit"}; }
inline StateKey<bool> OpenBit() { return {"open_bit"}; }
inline StateKey<bool> UpsideDownBit() { return {"upside_down_bit"}; }
inline StateKey<int> Direction() { return {"direction"}; }
}
struct Block;
inline int nativeCalls{};
struct BlockType {
    bool slab{}, doubleSlab{}, nativeAllows{};
    bool isSlabBlock() const { return slab; }
    bool allowStateMismatchOnPlacement(Block const&, Block const&) const {
        ++nativeCalls;
        return nativeAllows;
    }
};
enum class BlockProperty { Door };
struct Block {
    std::string name;
    CompoundTag serialized;
    BlockType type{};
    bool door{}, missingSerialization{};
    int runtimeVariant{};
    Block(std::string n, States s) : name(std::move(n)), serialized{std::move(s)} {}
    std::string const& getTypeName() const { return name; }
    BlockType const& getBlockType() const { return type; }
    bool hasProperty(BlockProperty) const { return door; }
    bool operator==(Block const& rhs) const {
        return name == rhs.name && serialized.mTags == rhs.serialized.mTags
            && runtimeVariant == rhs.runtimeVariant;
    }
    template<class T> std::optional<T> getState(StateKey<T> const& state) const {
        auto const found = serialized.mTags.find(state.key);
        if (found == serialized.mTags.end()) return std::nullopt;
        auto const& text = found->second;
        int value{};
        auto const result = std::from_chars(text.data(), text.data()+text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data()+text.size()) return std::nullopt;
        if constexpr (std::is_same_v<T, bool>) {
            if (value != 0 && value != 1) return std::nullopt;
        }
        return static_cast<T>(value);
    }
};
namespace lholo::place {
struct PlacementState { bool manual = true; bool manualMode() const { return manual; } };
inline PlacementState gState;
inline PlacementState& placementState() { return gState; }
inline std::string serializedState(Block const& b, char const* key) {
    auto const it = b.serialized.mTags.find(key);
    return it == b.serialized.mTags.end() ? std::string{} : it->second;
}
inline CompoundTag const* placementSerializedStates(Block const& b) {
    return b.missingSerialization ? nullptr : &b.serialized;
}
inline bool isDoubleSlab(Block const& b) { return b.type.slab && b.type.doubleSlab; }
}
namespace lholo::projection::detail {
inline CompoundTag const* serializedBlockStates(Block const& b) {
    return b.missingSerialization ? nullptr : &b.serialized;
}
// This test suite does not exercise sapling growth tolerance.
inline bool isVanillaSaplingType(std::string_view) { return false; }
}
