// LHolo - Manual placement policy (no Minecraft or renderer dependencies)
// Copyright (C) 2026  MarmieQi and contributors
#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace lholo::place::detail {

inline constexpr std::size_t kMaxManualPlacementAllowedItems = 128;
inline constexpr std::size_t kMaxManualPlacementItemIdLength = 255;

// Exact inventory IDs only. No wildcard, tag, block-state or NBT syntax.
// The empty list is the backwards-compatible, restrictive default.
inline std::optional<std::string> normalizeManualPlacementItemId(std::string_view input) {
    constexpr std::string_view whitespace = " \t\r\n";
    auto const begin = input.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) return std::nullopt;
    auto const end = input.find_last_not_of(whitespace);
    input = input.substr(begin, end - begin + 1);
    if (input.size() > kMaxManualPlacementItemIdLength) return std::nullopt;
    std::string id{input};
    for (char& c : id) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (id.find(':') == std::string::npos) id.insert(0, "minecraft:");
    if (id.size() > kMaxManualPlacementItemIdLength) return std::nullopt;
    auto const colon = id.find(':');
    if (colon == 0 || colon + 1 == id.size() || id.find(':', colon + 1) != std::string::npos) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < id.size(); ++i) {
        auto const c = id[i];
        if (i == colon) continue;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '_' || c == '-' || c == '.' || (i > colon && c == '/')) continue;
        return std::nullopt;
    }
    if (id == "minecraft:air" || id == "minecraft:cave_air" || id == "minecraft:void_air") {
        return std::nullopt;
    }
    return id;
}

inline std::optional<std::vector<std::string>> normalizeManualPlacementAllowedItems(
    std::vector<std::string> const& items
) {
    if (items.size() > kMaxManualPlacementAllowedItems) return std::nullopt;
    std::vector<std::string> normalized;
    normalized.reserve(items.size());
    for (auto const& item : items) {
        auto id = normalizeManualPlacementItemId(item);
        if (!id) return std::nullopt; // Reject the entire edit, not a partial permission change.
        normalized.push_back(std::move(*id));
    }
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

// Known vanilla blocks which compute their connections from their neighbors.
// Glass panes use separate block names for colors, so this never discards dye
// identity. Directional fence gates and user-controlled orientations are not
// included. Only this small allow-list may use the relaxed Auto/Range matcher.
inline constexpr bool isVanillaConnectivityPlacementBlock(std::string_view name) {
    if (!name.starts_with("minecraft:")) return false;
    name.remove_prefix(10);
    return name == "iron_bars" || name == "copper_bars"
        || name.ends_with("_copper_bars")
        || name == "glass_pane" || name.ends_with("_stained_glass_pane")
        || name == "fence" || name.ends_with("_fence")
        || name == "wall" || name.ends_with("_wall");
}

// The 1.26 Bedrock pane/bar connection flags are derived by the native engine
// after placement. Older structure conversions sometimes serialize the
// unnamespaced aliases; support only these exact cardinal names, not a broad
// 'connection_*' wildcard that might hide a custom/player-controlled state.
inline constexpr bool isConnectivityDerivedStateKey(std::string_view state) {
    return state == "minecraft:connection_north"
        || state == "minecraft:connection_south"
        || state == "minecraft:connection_east"
        || state == "minecraft:connection_west"
        || state == "connection_north"
        || state == "connection_south"
        || state == "connection_east"
        || state == "connection_west";
}

// Only known vanilla, environment-driven states are ignored. Never ignore a
// direction/axis, slab half, material variant, unknown state or custom block's
// state. Used by MANUAL placement and by the narrow vanilla connection-family
// Auto/Range fallback. The correction renderer stays strict.
inline bool isManualPlacementDerivedState(std::string_view name, std::string_view state) {
    if (!name.starts_with("minecraft:")) return false;
    if (isVanillaConnectivityPlacementBlock(name) &&
        isConnectivityDerivedStateKey(state)) return true;
    name.remove_prefix(10);
    bool const wall = name.ends_with("_wall");
    if (wall && (state == "wall_post_bit" || state == "wall_connection_type_north"
        || state == "wall_connection_type_south" || state == "wall_connection_type_east"
        || state == "wall_connection_type_west")) return true;
    if (name == "redstone_wire") return state == "redstone_signal";
    if (name.ends_with("_button")) return state == "button_pressed_bit";
    if (name == "lever" || name.ends_with("_trapdoor") || name == "trapdoor") return state == "open_bit";
    if (name.ends_with("_fence_gate") || name == "fence_gate") {
        return state == "open_bit" || state == "in_wall_bit";
    }
    if (name == "observer" || name.ends_with("lightning_rod")) {
        return state == "powered_bit";
    }
    if (name == "hopper" || name == "bell") return state == "toggle_bit";
    if (name == "dispenser" || name == "dropper") return state == "triggered_bit";
    if (name == "crafter") return state == "crafting" || state == "triggered_bit";
    if (name == "barrel") return state == "open_bit";
    if (name == "lectern") return state == "powered_bit";
    if (name == "sculk_sensor" || name == "calibrated_sculk_sensor") {
        return state == "sculk_sensor_phase";
    }
    if (name == "chiseled_bookshelf") return state == "books_stored";
    if (name.ends_with("_shelf")) {
        return state == "powered_bit" || state == "powered_shelf_type";
    }
    if (name.ends_with("copper_bulb")) return state == "lit" || state == "powered_bit";
    if (name.ends_with("_pressure_plate")) return state == "redstone_signal";
    if (name == "golden_rail" || name == "detector_rail" || name == "activator_rail") {
        return state == "rail_data_bit"; // rail_direction remains strict.
    }
    if (name == "trip_wire" || name == "tripwire" || name == "tripwire_hook") {
        return state == "attached_bit" || state == "powered_bit" || state == "suspended_bit";
    }
    if (name == "scaffolding") return state == "stability" || state == "stability_check";
    return false;
}

// Serialized maps omit Bedrock's internal flattened fence/pane connections.
// Equal serialized maps can therefore describe a correct placement even when
// runtime IDs differ. Compare in both directions; do not discard unknown keys.
template <typename StateMap>
bool manualPlacementStateMapsMatch(
    std::string_view name,
    StateMap const& expected,
    StateMap const& predicted
) {
    std::size_t expectedCount{};
    std::size_t predictedCount{};
    for (auto const& [key, value] : expected) {
        if (isManualPlacementDerivedState(name, key)) continue;
        ++expectedCount;
        auto const found = predicted.find(key);
        if (found == predicted.end() || !(value == found->second)) return false;
    }
    for (auto const& [key, value] : predicted) {
        (void)value;
        if (!isManualPlacementDerivedState(name, key)) ++predictedCount;
    }
    return expectedCount == predictedCount;
}

} // namespace lholo::place::detail
