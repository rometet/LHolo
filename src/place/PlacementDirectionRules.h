// LHolo - targeted assisted-placement direction policy
#pragma once

#include <string_view>
#include <optional>
#include <cstdint>

namespace lholo::place::detail {

enum class PlacementDirectionRule {
    None,
    Facing,
    Horizontal,
    Orientation,
    Lever,
    Bell,
    Trapdoor,
};

inline constexpr PlacementDirectionRule placementDirectionRule(std::string_view name) {
    if (name == "minecraft:hopper"
        || name == "minecraft:observer"
        || name == "minecraft:dispenser"
        || name == "minecraft:dropper"
        || name == "minecraft:piston"
        || name == "minecraft:sticky_piston"
        || name == "minecraft:barrel"
        || name.ends_with("lightning_rod")
        || name.ends_with("_button")) {
        return PlacementDirectionRule::Facing;
    }
    if (name == "minecraft:unpowered_repeater"
        || name == "minecraft:powered_repeater"
        || name == "minecraft:unpowered_comparator"
        || name == "minecraft:powered_comparator"
        || name == "minecraft:tripwire_hook"
        || name == "minecraft:lectern"
        || name == "minecraft:calibrated_sculk_sensor"
        || name == "minecraft:chiseled_bookshelf"
        || name == "minecraft:fence_gate"
        || name.ends_with("_fence_gate")
        || name.ends_with("_shelf")) {
        return PlacementDirectionRule::Horizontal;
    }
    if (name == "minecraft:crafter") return PlacementDirectionRule::Orientation;
    if (name == "minecraft:lever") return PlacementDirectionRule::Lever;
    if (name == "minecraft:bell") return PlacementDirectionRule::Bell;
    if (name == "minecraft:trapdoor" || name.ends_with("_trapdoor")) {
        return PlacementDirectionRule::Trapdoor;
    }
    return PlacementDirectionRule::None;
}

inline constexpr bool isEnvironmentOnlyPlacementBlock(std::string_view name) {
    return name == "minecraft:redstone_wire"
        || name == "minecraft:trip_wire"
        || name == "minecraft:tripwire"
        || name == "minecraft:daylight_detector"
        || name == "minecraft:daylight_detector_inverted"
        || name == "minecraft:sculk_sensor"
        || name.ends_with("copper_bulb")
        || name.ends_with("_pressure_plate");
}

// Pure serialized-state selection used by the native prediction path. Both
// accessors return an empty string for an absent/unsupported state.
template <class Predicted, class Expected>
bool placementDirectionStatesMatch(PlacementDirectionRule rule, Predicted predicted, Expected expected) {
    auto same = [&](char const* key) {
        auto const value = expected(key);
        return !value.empty() && predicted(key) == value;
    };
    auto first = [&](char const* primary, char const* fallback) {
        return !expected(primary).empty() ? same(primary) : same(fallback);
    };
    switch (rule) {
    case PlacementDirectionRule::Facing: return first("minecraft:facing_direction", "facing_direction");
    case PlacementDirectionRule::Horizontal: return first("minecraft:cardinal_direction", "direction");
    case PlacementDirectionRule::Orientation: return same("orientation");
    case PlacementDirectionRule::Lever: return same("lever_direction");
    case PlacementDirectionRule::Bell: return same("direction") && same("attachment");
    case PlacementDirectionRule::Trapdoor: return same("direction") && same("upside_down_bit");
    case PlacementDirectionRule::None: return false;
    }
    return false;
}

inline constexpr std::optional<std::uint8_t> deterministicSupportFace(
    std::string_view name, std::string_view facing, std::string_view torchFacing
) {
    if (name == "minecraft:hopper") {
        if (facing == "0" || facing == "down") return 0;
        if (facing == "2" || facing == "north") return 2;
        if (facing == "3" || facing == "south") return 3;
        if (facing == "4" || facing == "west") return 4;
        if (facing == "5" || facing == "east") return 5;
        return std::nullopt;
    }
    if (torchFacing == "top") return 0;
    if (torchFacing == "north") return 2;
    if (torchFacing == "south") return 3;
    if (torchFacing == "west") return 4;
    if (torchFacing == "east") return 5;
    return std::nullopt;
}
inline constexpr bool isPlacementControlledStateKey(std::string_view key) {
    return key == "direction"
        || key == "minecraft:cardinal_direction"
        || key == "facing_direction"
        || key == "minecraft:facing_direction"
        || key == "orientation"
        || key == "pillar_axis"
        || key == "weirdo_direction"
        || key == "upside_down_bit"
        || key == "top_slot_bit"
        || key == "minecraft:vertical_half"
        || key == "torch_facing_direction"
        || key == "ground_sign_direction"
        || key == "rail_direction"
        || key == "lever_direction"
        || key == "attachment"
        || key == "minecraft:block_face"
        || key == "vertical_direction"
        || key == "upper_block_bit"
        || key == "head_piece_bit";
}

} // namespace lholo::place::detail
