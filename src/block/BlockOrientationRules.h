// LHolo - Serialized orientation invariants shared by placement and correction
// Copyright (C) 2026 MarmieQi and contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace lholo::block {

// Exact state keys, not block-name heuristics. Connection, power, open, delay,
// and growth states are deliberately absent: this is a veto, not permission
// to place, and the existing family/native/manual checks still run afterward.
inline constexpr bool isPlacementOrientationState(std::string_view key) {
    return key == "direction" || key == "minecraft:cardinal_direction"
        || key == "facing_direction" || key == "minecraft:facing_direction"
        || key == "orientation" || key == "pillar_axis"
        || key == "weirdo_direction" || key == "upside_down_bit"
        || key == "top_slot_bit" || key == "minecraft:vertical_half"
        || key == "torch_facing_direction" || key == "ground_sign_direction"
        || key == "rail_direction" || key == "lever_direction"
        || key == "attachment" || key == "minecraft:block_face"
        || key == "vertical_direction" || key == "hanging"
        || key == "upper_block_bit" || key == "head_piece_bit";
}

inline constexpr bool isHorizontalDirectionState(std::string_view key) {
    return key == "direction" || key == "minecraft:cardinal_direction";
}

// Tag equality retains the serialized value AND type in production. Check both
// directions so a missing expected/actual key cannot become a wildcard. Do not
// translate integer directions into strings: their encoding is block-specific.
template <class States, class IsProtected>
bool protectedStatesMatch(States const& expected, States const& actual, IsProtected protect) {
    for (auto const& [key, value] : expected) {
        if (!protect(key)) continue;
        auto const found = actual.find(key);
        if (found == actual.end() || !(value == found->second)) return false;
    }
    for (auto const& [key, value] : actual) {
        (void)value;
        if (protect(key) && expected.find(key) == expected.end()) return false;
    }
    return true;
}

template <class States>
bool placementOrientationStatesMatch(
    std::string_view name, States const& expected, States const& actual
) {
    // attached_bit selects a hanging-sign attachment, but on tripwire it is
    // neighbour-derived. Never turn it into a global placement invariant.
    bool const hangingSign = name.starts_with("minecraft:") && name.ends_with("_hanging_sign");
    return protectedStatesMatch(expected, actual, [hangingSign](std::string_view key) {
        return isPlacementOrientationState(key) || (hangingSign && key == "attached_bit");
    });
}

// Doors/repeaters may expose the old or the new key in different registries.
// Require at least one recognized key, and require ALL supplied direction keys
// to agree. Unknown, absent, or mixed unmatched schemas fail closed.
template <class States>
bool horizontalDirectionStatesMatch(States const& expected, States const& actual) {
    bool hasDirection = false;
    for (auto const& [key, value] : expected) {
        (void)value;
        if (isHorizontalDirectionState(key)) hasDirection = true;
    }
    return hasDirection && protectedStatesMatch(expected, actual, isHorizontalDirectionState);
}

} // namespace lholo::block
