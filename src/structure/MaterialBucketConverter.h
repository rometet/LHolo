// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// JE-compatible material substitution and build-quantity rules.
//
// PORTED FROM LeviSchematic (LGPL-3.0, tag v1.2.0, commit e6f803b):
//   src/levischematic/interaction/survival/inventory/MaterialRules.h
//     ::levischematic::survival::substituteItem
//     ::levischematic::survival::buildRule
// See THIRD_PARTY_NOTICES.md and licenses/LeviSchematic/LICENSE.
//
// The port is deliberately engine-free: LeviSchematic reads block states through
// verifier::buildCompareSpecFromBlock and item identity through the Bedrock item
// registry. Here the caller supplies a plain state map and this module returns a
// *name* only, so the rules are unit-testable without a game process. Item
// registry resolution and localization stay in lholo::block::resolvePlacementItem.
//
// Keeping these rules pure also prevents drift: the material tracker and export
// share exactly one implementation of the bucket/quantity semantics.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace lholo::structure::material_rules {

using BlockStateMap = std::map<std::string, int>;

struct BuildQuantityRule {
    unsigned count{1};
    // Secondary halves of multi-block objects (doors, beds, tall plants) are
    // counted once against the primary half.
    bool secondary{false};
    std::array<int, 3> partnerOffset{0, 0, 0};
};

// Converts a block into the inventory item a player must gather instead.
//   - an engaged optional containing an item name means "use this item"
//   - an engaged optional containing an empty string means "needs no item"
//   - nullopt means "use the block's normal block-to-item mapping"
[[nodiscard]] inline std::optional<std::string> substituteItem(
    std::string_view name,
    BlockStateMap const& states
) {
    auto depth = [&] {
        auto it = states.find("liquid_depth");
        return it == states.end() ? 0 : it->second;
    };
    // Only a source (falling) fluid is replaced by an item; flowing fluid levels
    // are already covered by the block that produced them.
    if (name == "minecraft:water" || name == "minecraft:flowing_water")
        return depth() == 0 ? std::string{"minecraft:water_bucket"} : std::string{};
    if (name == "minecraft:lava" || name == "minecraft:flowing_lava")
        return depth() == 0 ? std::string{"minecraft:lava_bucket"} : std::string{};
    if (name == "minecraft:bubble_column") return std::string{"minecraft:water_bucket"};
    if (name == "minecraft:powder_snow") return std::string{"minecraft:powder_snow_bucket"};
    // These two drop dirt, so a player builds them by placing dirt.
    if (name == "minecraft:farmland" || name == "minecraft:grass_path") return std::string{"minecraft:dirt"};

    // Engine-generated or unplaceable blocks. The player cannot obtain them.
    for (auto none : {
             "minecraft:fire",
             "minecraft:soul_fire",
             "minecraft:portal",
             "minecraft:end_portal",
             "minecraft:end_gateway",
             "minecraft:piston_arm_collision",
             "minecraft:sticky_piston_arm_collision",
             "minecraft:moving_block",
             "minecraft:frosted_ice",
         })
        if (name == none) return std::string{};
    return std::nullopt;
}

// Returns how many inventory items one instance of this block state consumes,
// and whether the cell is the secondary half of a multi-block object.
[[nodiscard]] inline BuildQuantityRule buildQuantity(
    std::string_view name,
    BlockStateMap const& states
) {
    auto value = [&](char const* key, int fallback = 0) {
        auto it = states.find(key);
        return it == states.end() ? fallback : it->second;
    };
    BuildQuantityRule result;
    if (states.contains("upper_block_bit")) {
        result.secondary     = value("upper_block_bit") != 0;
        result.partnerOffset = {0, result.secondary ? -1 : 1, 0};
    }
    if (states.contains("head_piece_bit")) {
        result.secondary             = value("head_piece_bit") != 0;
        int direction                = value("direction", value("minecraft:cardinal_direction", -1));
        constexpr std::array<std::array<int, 3>, 4> steps{
            {{0, 0, 1}, {-1, 0, 0}, {0, 0, -1}, {1, 0, 0}}
        };
        if (direction >= 0 && direction < 4) {
            result.partnerOffset = steps[static_cast<std::size_t>(direction)];
            if (result.secondary)
                for (auto& axis : result.partnerOffset) axis = -axis;
        }
    }
    if (name.find("double_") != std::string_view::npos && name.find("slab") != std::string_view::npos)
        result.count = 2;
    if (name == "minecraft:snow_layer")
        result.count = static_cast<unsigned>(std::clamp(value("height") + 1, 1, 8));
    if (name == "minecraft:turtle_egg")
        result.count = static_cast<unsigned>(std::clamp(value("turtle_egg_count") + 1, 1, 4));
    if (name == "minecraft:sea_pickle")
        result.count = static_cast<unsigned>(std::clamp(value("cluster_count") + 1, 1, 4));
    if (name.ends_with("candle") && !name.ends_with("candle_cake"))
        result.count = static_cast<unsigned>(std::clamp(value("candles") + 1, 1, 4));
    return result;
}

} // namespace lholo::structure::material_rules
