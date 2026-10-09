// Regression tests shared by LHoloLogicTests and the portable audit runner.
#pragma once
#include "place/ManualPlacementRules.h"
#include "place/PlacementDirectionRules.h"
#include "place/PlacementState.h"

#include <map>
#include <future>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace lholo::tests {
template <typename Check>
void runManualPlacementChecks(Check check) {
    using namespace place::detail;

    using Rule = PlacementDirectionRule;
    check(placementDirectionRule("minecraft:hopper") == Rule::Facing);
    check(placementDirectionRule("minecraft:observer") == Rule::Facing);
    check(placementDirectionRule("minecraft:dispenser") == Rule::Facing);
    check(placementDirectionRule("minecraft:dropper") == Rule::Facing);
    check(placementDirectionRule("minecraft:piston") == Rule::Facing);
    check(placementDirectionRule("minecraft:sticky_piston") == Rule::Facing);
    check(placementDirectionRule("minecraft:barrel") == Rule::Facing);
    check(placementDirectionRule("minecraft:lightning_rod") == Rule::Facing);
    check(placementDirectionRule("minecraft:waxed_oxidized_lightning_rod") == Rule::Facing);
    check(placementDirectionRule("minecraft:stone_button") == Rule::Facing);
    check(placementDirectionRule("minecraft:unpowered_repeater") == Rule::Horizontal);
    check(placementDirectionRule("minecraft:powered_comparator") == Rule::Horizontal);
    check(placementDirectionRule("minecraft:tripwire_hook") == Rule::Horizontal);
    check(placementDirectionRule("minecraft:lectern") == Rule::Horizontal);
    check(placementDirectionRule("minecraft:calibrated_sculk_sensor") == Rule::Horizontal);
    check(placementDirectionRule("minecraft:chiseled_bookshelf") == Rule::Horizontal);
    check(placementDirectionRule("minecraft:oak_fence_gate") == Rule::Horizontal);
    check(placementDirectionRule("minecraft:bamboo_shelf") == Rule::Horizontal);
    check(placementDirectionRule("minecraft:crafter") == Rule::Orientation);
    check(placementDirectionRule("minecraft:lever") == Rule::Lever);
    check(placementDirectionRule("minecraft:bell") == Rule::Bell);
    check(placementDirectionRule("minecraft:oak_trapdoor") == Rule::Trapdoor);
    check(placementDirectionRule("minecraft:stone") == Rule::None);
    check(isEnvironmentOnlyPlacementBlock("minecraft:redstone_wire"));
    check(isEnvironmentOnlyPlacementBlock("minecraft:heavy_weighted_pressure_plate"));
    check(isEnvironmentOnlyPlacementBlock("minecraft:trip_wire"));
    check(isEnvironmentOnlyPlacementBlock("minecraft:tripwire"));
    check(isEnvironmentOnlyPlacementBlock("minecraft:sculk_sensor"));
    check(isEnvironmentOnlyPlacementBlock("minecraft:copper_bulb"));
    check(isEnvironmentOnlyPlacementBlock("minecraft:waxed_oxidized_copper_bulb"));
    check(!isEnvironmentOnlyPlacementBlock("minecraft:hopper"));
    for (auto state : {
             "direction", "minecraft:cardinal_direction", "facing_direction",
             "minecraft:facing_direction", "orientation", "pillar_axis",
             "weirdo_direction", "upside_down_bit", "minecraft:vertical_half",
             "torch_facing_direction", "rail_direction", "lever_direction"
         }) {
        check(isPlacementControlledStateKey(state));
    }
    for (auto state : {"redstone_signal", "powered_bit", "triggered_bit", "toggle_bit", "open_bit"}) {
        check(!isPlacementControlledStateKey(state));
    }

    check(normalizeManualPlacementItemId(" dirt ") == "minecraft:dirt");
    check(normalizeManualPlacementItemId("MINECRAFT:SCAFFOLDING") == "minecraft:scaffolding");
    check(normalizeManualPlacementItemId("addon:temporary/block") == "addon:temporary/block");
    for (auto id : {"", " ", "*", "minecraft:*", "#minecraft:logs", ":dirt", "minecraft:",
                    "a:b:c", "minecraft:air", "minecraft:cave_air", "minecraft:void_air",
                    "stone[axis=x]", "stone{}", "minecraft:stone dirt", "日本語"}) {
        check(!normalizeManualPlacementItemId(id));
    }
    check(!normalizeManualPlacementItemId(std::string(256, 'a')));
    auto empty = normalizeManualPlacementAllowedItems({});
    check(empty && empty->empty());
    auto items = normalizeManualPlacementAllowedItems({"dirt", "minecraft:dirt", " SCAFFOLDING "});
    check(items && *items == (std::vector<std::string>{"minecraft:dirt", "minecraft:scaffolding"}));
    check(!normalizeManualPlacementAllowedItems({"dirt", "*"}));
    check(!normalizeManualPlacementAllowedItems(std::vector<std::string>(129, "dirt")));

    using States = std::map<std::string, int>;
    // Category matrix: matching serialized states, every placement-controlled
    // field changed, and unknown/missing/extra fields in both directions.
    std::vector<std::pair<std::string, std::vector<std::string>>> const categories{
        {"stone", {}}, {"oak_slab", {"minecraft:vertical_half"}},
        {"stone_block_slab", {"top_slot_bit"}}, {"oak_stairs", {"weirdo_direction", "upside_down_bit"}},
        {"oak_trapdoor", {"direction", "upside_down_bit"}},
        {"oak_door", {"minecraft:cardinal_direction", "upper_block_bit", "door_hinge_bit"}},
        {"oak_fence_gate", {"direction"}}, {"stone_button", {"facing_direction"}},
        {"lever", {"lever_direction"}}, {"redstone_torch", {"torch_facing_direction"}},
        {"unpowered_repeater", {"direction", "repeater_delay"}},
        {"unpowered_comparator", {"direction", "output_subtract_bit"}},
        {"observer", {"facing_direction"}}, {"piston", {"facing_direction"}},
        {"sticky_piston", {"facing_direction"}}, {"dropper", {"facing_direction"}},
        {"dispenser", {"facing_direction"}}, {"hopper", {"facing_direction"}},
        {"oak_log", {"pillar_axis"}}, {"quartz_pillar", {"pillar_axis"}},
        {"rail", {"rail_direction"}}, {"standing_sign", {"ground_sign_direction"}},
        {"wall_sign", {"facing_direction"}},
        {"oak_hanging_sign", {"facing_direction", "hanging", "attached_bit"}},
        {"bed", {"direction", "head_piece_bit"}}, {"chest", {"facing_direction"}},
        {"barrel", {"facing_direction"}}, {"bell", {"direction", "attachment"}},
        {"anvil", {"direction"}}, {"red_glazed_terracotta", {"facing_direction"}},
        {"grindstone", {"direction", "attachment"}}, {"lightning_rod", {"facing_direction"}},
        {"crafter", {"orientation"}}, {"command_block", {"facing_direction", "conditional_bit"}}
    };
    for (auto const& [localName, keys] : categories) {
        auto const name = "minecraft:" + localName;
        States expected;
        for (auto const& key : keys) expected[key] = 0;
        check(manualPlacementStateMapsMatch(name, expected, expected));
        for (auto const& key : keys) {
            for (int value = 1; value <= 5; ++value) {
                auto wrong = expected; wrong[key] = value;
                check(!manualPlacementStateMapsMatch(name, expected, wrong));
                check(!manualPlacementStateMapsMatch(name, wrong, expected));
            }
            auto absent = expected; absent.erase(key);
            check(!manualPlacementStateMapsMatch(name, expected, absent));
        }
        auto extra = expected; extra["unknown_state"] = 1;
        check(!manualPlacementStateMapsMatch(name, expected, extra));
        check(!manualPlacementStateMapsMatch(name, extra, expected));
    }
    using TextStates = std::map<std::string, std::string>;
    auto directionMatches = [](Rule rule, TextStates const& expected, TextStates const& predicted) {
        auto accessor = [](TextStates const& states, char const* key) {
            auto const found = states.find(key);
            return found == states.end() ? std::string{} : found->second;
        };
        return placementDirectionStatesMatch(rule,
            [&](char const* key) { return accessor(predicted, key); },
            [&](char const* key) { return accessor(expected, key); });
    };
    for (auto const& [rule, key] : std::vector<std::pair<Rule, std::string>>{
        {Rule::Facing, "minecraft:facing_direction"}, {Rule::Facing, "facing_direction"},
        {Rule::Horizontal, "minecraft:cardinal_direction"}, {Rule::Horizontal, "direction"},
        {Rule::Orientation, "orientation"}, {Rule::Lever, "lever_direction"}}) {
        for (auto const& value : {"0", "1", "2", "3", "4", "5", "north", "south", "east", "west"}) {
            TextStates const expected{{key, value}};
            check(directionMatches(rule, expected, expected));
            check(!directionMatches(rule, expected, {{key, "wrong"}}));
            check(!directionMatches(rule, expected, {}));
        }
        check(!directionMatches(rule, {}, {}));
    }
    for (auto const& [rule, keys] : std::vector<std::pair<Rule, std::vector<std::string>>>{
        {Rule::Bell, {"direction", "attachment"}}, {Rule::Trapdoor, {"direction", "upside_down_bit"}}}) {
        for (int direction = 0; direction < 4; ++direction) {
            for (int half = 0; half < 2; ++half) {
                TextStates const expected{{keys[0], std::to_string(direction)}, {keys[1], std::to_string(half)}};
                auto actual = expected; actual["open_bit"] = "1";
                check(directionMatches(rule, expected, actual));
                actual[keys[0]] = std::to_string((direction + 1) % 4);
                check(!directionMatches(rule, expected, actual));
                actual = expected; actual.erase(keys[1]);
                check(!directionMatches(rule, expected, actual));
            }
        }
    }
    check(!directionMatches(Rule::None, {}, {}));
    check(!directionMatches(Rule::Facing, {{"minecraft:facing_direction", "north"}, {"facing_direction", "2"}},
        {{"minecraft:facing_direction", "south"}, {"facing_direction", "2"}}));
    for (auto const& [state, face] : std::vector<std::pair<std::string, std::uint8_t>>{
        {"0", 0}, {"down", 0}, {"2", 2}, {"north", 2}, {"3", 3}, {"south", 3},
        {"4", 4}, {"west", 4}, {"5", 5}, {"east", 5}}) {
        check(deterministicSupportFace("minecraft:hopper", state, "") == face);
    }
    check(!deterministicSupportFace("minecraft:hopper", "up", ""));
    check(!deterministicSupportFace("minecraft:hopper", "1", ""));
    for (auto const& [state, face] : std::vector<std::pair<std::string, std::uint8_t>>{
        {"top", 0}, {"north", 2}, {"south", 3}, {"west", 4}, {"east", 5}}) {
        check(deterministicSupportFace("minecraft:redstone_torch", "", state) == face);
    }
    check(!deterministicSupportFace("minecraft:redstone_torch", "", "unknown"));
    States const same{{"facing_direction", 2}, {"color", 14}};
    for (auto name : {"minecraft:oak_fence", "minecraft:iron_bars", "minecraft:glass_pane",
                      "minecraft:red_stained_glass_pane", "addon:custom_block"}) {
        // Equal serialized identity is not defeated by an external runtime-ID difference.
        check(manualPlacementStateMapsMatch(name, same, same));
        auto wrong = same; wrong["color"] = 3;
        check(!manualPlacementStateMapsMatch(name, same, wrong));
        wrong = same; wrong["facing_direction"] = 4;
        check(!manualPlacementStateMapsMatch(name, same, wrong));
        wrong = same; wrong["unknown_state"] = 1;
        check(!manualPlacementStateMapsMatch(name, same, wrong));
        check(!manualPlacementStateMapsMatch(name, wrong, same));
    }
    std::vector<std::pair<std::string, std::string>> const derived{
        {"minecraft:cobblestone_wall", "wall_post_bit"},
        {"minecraft:stone_brick_wall", "wall_connection_type_north"},
        {"minecraft:stone_brick_wall", "wall_connection_type_south"},
        {"minecraft:stone_brick_wall", "wall_connection_type_east"},
        {"minecraft:stone_brick_wall", "wall_connection_type_west"},
        {"minecraft:redstone_wire", "redstone_signal"},
        {"minecraft:stone_button", "button_pressed_bit"},
        {"minecraft:lever", "open_bit"},
        {"minecraft:oak_trapdoor", "open_bit"},
        {"minecraft:trapdoor", "open_bit"},
        {"minecraft:oak_fence_gate", "in_wall_bit"},
        {"minecraft:fence_gate", "open_bit"},
        {"minecraft:observer", "powered_bit"},
        {"minecraft:lightning_rod", "powered_bit"},
        {"minecraft:hopper", "toggle_bit"},
        {"minecraft:bell", "toggle_bit"},
        {"minecraft:dispenser", "triggered_bit"},
        {"minecraft:dropper", "triggered_bit"},
        {"minecraft:crafter", "crafting"},
        {"minecraft:crafter", "triggered_bit"},
        {"minecraft:barrel", "open_bit"},
        {"minecraft:lectern", "powered_bit"},
        {"minecraft:sculk_sensor", "sculk_sensor_phase"},
        {"minecraft:calibrated_sculk_sensor", "sculk_sensor_phase"},
        {"minecraft:chiseled_bookshelf", "books_stored"},
        {"minecraft:bamboo_shelf", "powered_bit"},
        {"minecraft:bamboo_shelf", "powered_shelf_type"},
        {"minecraft:copper_bulb", "lit"},
        {"minecraft:copper_bulb", "powered_bit"},
        {"minecraft:heavy_weighted_pressure_plate", "redstone_signal"},
        {"minecraft:golden_rail", "rail_data_bit"},
        {"minecraft:activator_rail", "rail_data_bit"},
        {"minecraft:detector_rail", "rail_data_bit"},
        {"minecraft:tripwire_hook", "attached_bit"},
        {"minecraft:trip_wire", "powered_bit"},
        {"minecraft:trip_wire", "suspended_bit"},
        {"minecraft:tripwire", "powered_bit"},
        {"minecraft:scaffolding", "stability"},
        {"minecraft:scaffolding", "stability_check"}
    };
    for (auto const& [name, state] : derived) {
        check(isManualPlacementDerivedState(name, state));
        States const expected{{state, 1}, {"facing_direction", 2}};
        States predicted{{state, 0}, {"facing_direction", 2}};
        check(manualPlacementStateMapsMatch(name, expected, predicted));
        check(manualPlacementStateMapsMatch(name, predicted, expected));
        predicted["facing_direction"] = 3;
        check(!manualPlacementStateMapsMatch(name, expected, predicted));
        check(!isManualPlacementDerivedState("addon:" + name.substr(10), state));
        check(!isManualPlacementDerivedState("minecraft:unrelated", state));
    }
    for (auto state : {"facing_direction", "minecraft:cardinal_direction", "direction", "pillar_axis",
                       "weirdo_direction", "upside_down_bit", "top_slot_bit", "minecraft:vertical_half",
                       "door_hinge_bit", "upper_block_bit", "rail_direction", "color", "unknown_state"}) {
        for (auto const& [name, ignored] : derived) {
            (void)ignored;
            check(!isManualPlacementDerivedState(name, state));
            check(!manualPlacementStateMapsMatch(name, States{{state, 1}}, States{{state, 2}}));
        }
    }

    auto& runtime = PlacementState::getInstance();
    runtime.setManualPlacementAllowedItems({});
    runtime.resetWorldSession();
    check(!runtime.manualPlacementItemAllowed("minecraft:dirt"));
    check(runtime.setManualPlacementAllowedItems({"dirt", "SCAFFOLDING"}));
    check(!runtime.setManualPlacementAllowedItems({"scaffolding", "minecraft:dirt"}));
    check(runtime.manualPlacementItemAllowed("minecraft:dirt"));
    check(runtime.manualPlacementItemAllowed("minecraft:scaffolding"));
    check(!runtime.manualPlacementItemAllowed("minecraft:coarse_dirt"));
    check(!runtime.manualPlacementItemAllowed("minecraft:air"));
    auto const before = runtime.manualPlacementAllowedItems();
    check(!runtime.setManualPlacementAllowedItems({"*"}));
    check(runtime.manualPlacementAllowedItems() == before);
    runtime.setManualMode(true);
    check(runtime.beginManualPress(100, runtime.manualInputEpoch()));
    check(runtime.manualHeld() && runtime.manualPlaceRequested());
    check(runtime.setManualPlacementAllowedItems({"dirt"}));
    check(!runtime.manualHeld() && !runtime.manualPlaceRequested());
    check(runtime.beginManualPress(200, runtime.manualInputEpoch()));
    // The same cancellation used at all vanilla-bypass boundaries clears repeats and queued taps.
    runtime.cancelManualPress();
    check(!runtime.manualHeld() && !runtime.manualPlaceRequested());
    runtime.resetDimensionSession();
    check(runtime.manualPlacementItemAllowed("minecraft:dirt"));
    runtime.resetWorldSession();
    check(runtime.manualPlacementItemAllowed("minecraft:dirt"));
    check(!runtime.manualMode());
    check(runtime.setManualPlacementAllowedItems({}));
    check(!runtime.manualPlacementItemAllowed("minecraft:dirt"));

    // A native press can finish its target/inventory lookup after Present has
    // canceled it. Hold the real state operation at that publication boundary;
    // no scheduler timing or simulated Minecraft implementation is involved.
    for (int round = 0; round < 32; ++round) {
        runtime.resetWorldSession();
        runtime.setManualPlacementAllowedItems({});
        runtime.setManualMode(true);
        std::promise<void> admitted, resume;
        auto ready = admitted.get_future();
        auto go = resume.get_future();
        bool accepted{};
        std::thread oldPress([&] {
            auto const epoch = runtime.manualInputEpoch();
            auto const wasManual = runtime.manualMode();
            admitted.set_value();
            go.wait();
            accepted = wasManual && runtime.beginManualPress(100, epoch);
        });
        ready.wait();
        switch (round % 4) {
        case 0: runtime.cancelManualPress(); break;
        case 1: runtime.resetManualInput(); break;
        case 2: runtime.setManualMode(false); runtime.setManualMode(true); break;
        case 3: runtime.setManualPlacementAllowedItems({"dirt"}); break;
        }
        resume.set_value();
        oldPress.join();
        check(!accepted);
        check(!runtime.manualHeld());
        check(!runtime.manualPlaceRequested());
        check(runtime.beginManualPress(200, runtime.manualInputEpoch()));
    }
    runtime.setManualMode(false);
    check(!runtime.manualHeld() && !runtime.manualPlaceRequested());
    check(!runtime.beginManualPress(300, runtime.manualInputEpoch()));
    runtime.setManualMode(true);
    check(runtime.beginManualPress(400, runtime.manualInputEpoch()));
    runtime.setManualMode(true); // Idempotent mode application preserves a live press.
    check(runtime.manualHeld() && runtime.manualPressAt() == 400);
    runtime.releaseManualPress();
    check(!runtime.manualHeld() && runtime.manualPlaceRequested()); // Quick tap survives release.
    runtime.resetWorldSession();
    runtime.setManualPlacementAllowedItems({});

    // Menu build/apply can straddle the render owner's world reset. A model
    // from that retired session must never restore an assisted-placement mode.
    for (int round = 0; round < 32; ++round) {
        auto const mode = round % 3;
        runtime.resetWorldSession();
        runtime.setEnabled(mode == 0);
        runtime.setManualMode(mode == 1);
        runtime.setRangeEnabled(mode == 2);
        std::promise<void> built, resume;
        auto ready = built.get_future();
        auto go = resume.get_future();
        bool accepted{};
        std::thread oldMenu([&] {
            auto const modes = runtime.modes();
            built.set_value();
            go.wait();
            accepted = runtime.applyModes(modes);
        });
        ready.wait();
        runtime.resetWorldSession();
        resume.set_value();
        oldMenu.join();
        check(!accepted);
        check(!runtime.enabled());
        check(!runtime.manualMode());
        check(!runtime.rangeEnabled());
        auto fresh = runtime.modes();
        fresh.enabled = mode == 0;
        fresh.manual = mode == 1;
        fresh.range = mode == 2;
        check(runtime.applyModes(fresh));
        auto const published = runtime.modes();
        check(published.enabled == fresh.enabled && published.manual == fresh.manual
            && published.range == fresh.range);
    }
    runtime.resetWorldSession();
    auto disabledModel = runtime.modes();
    disabledModel.enabled = true; // Clicked after this old model was built.
    runtime.resetWorldSession();
    check(!runtime.applyModes(disabledModel));
    check(!runtime.enabled());
    runtime.setManualMode(true);
    auto live = runtime.modes();
    check(runtime.beginManualPress(500, runtime.manualInputEpoch()));
    check(runtime.applyModes(live));
    check(runtime.manualHeld() && runtime.manualPressAt() == 500);
    runtime.resetDimensionSession();
    check(runtime.applyModes(live)); // Dimension suspension preserves modes.
    check(runtime.manualMode());
    runtime.setRangeEnabled(true);
    check(!runtime.applyModes(live)); // A newer explicit choice also wins.
    check(runtime.rangeEnabled());
    runtime.resetWorldSession();
    runtime.setManualMode(true);
    auto inputEpoch = runtime.manualInputEpoch();
    check(runtime.beginManualPress(600, inputEpoch));
    runtime.setRangeEnabled(true);
    check(!runtime.manualHeld() && !runtime.manualPlaceRequested());
    check(runtime.manualInputEpoch() != inputEpoch);
    check(!runtime.beginManualPress(650, inputEpoch));
    check(runtime.beginManualPress(700, runtime.manualInputEpoch()));
    runtime.setRangeEnabled(true); // Idempotent apply must preserve current input.
    check(runtime.manualHeld() && runtime.manualPressAt() == 700);
    runtime.setEnabled(true);
    check(!runtime.manualHeld() && !runtime.manualPlaceRequested());
    check(runtime.beginManualPress(800, runtime.manualInputEpoch()));
    auto newMode = runtime.modes();
    newMode.range = false; // Manual flag unchanged, executor route changes.
    check(runtime.applyModes(newMode));
    check(!runtime.manualHeld() && !runtime.manualPlaceRequested());
    runtime.resetWorldSession();
}
} // namespace lholo::tests
