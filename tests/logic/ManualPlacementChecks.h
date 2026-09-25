// Regression tests shared by LHoloLogicTests and the portable audit runner.
#pragma once
#include "place/ManualPlacementRules.h"
#include "place/PlacementState.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lholo::tests {
template <typename Check>
void runManualPlacementChecks(Check check) {
    using namespace place::detail;
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
        {"minecraft:hopper", "toggle_bit"},
        {"minecraft:heavy_weighted_pressure_plate", "redstone_signal"},
        {"minecraft:golden_rail", "rail_data_bit"},
        {"minecraft:activator_rail", "rail_data_bit"},
        {"minecraft:detector_rail", "rail_data_bit"},
        {"minecraft:tripwire_hook", "attached_bit"},
        {"minecraft:tripwire", "powered_bit"},
        {"minecraft:tripwire", "suspended_bit"},
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
    check(runtime.beginManualPress(100));
    check(runtime.manualHeld() && runtime.manualPlaceRequested());
    check(runtime.setManualPlacementAllowedItems({"dirt"}));
    check(!runtime.manualHeld() && !runtime.manualPlaceRequested());
    check(runtime.beginManualPress(200));
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
}
} // namespace lholo::tests
