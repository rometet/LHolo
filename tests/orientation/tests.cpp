// Production policy + current function bodies; the engine boundary is doubled.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine_doubles.h"
#include <cstdio>
#include <variant>
#include <vector>
#include "production.inc"

namespace {
int checks{}, failures{};
void check(char const* label, bool ok) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
using lholo::place::placementPredictionMatches;
using lholo::place::gState;
using lholo::projection::detail::projectionStatesMatch;
void genericCases() {
    // Names identify representative consumers, not evidence of native behavior.
    std::vector<std::pair<std::string, std::string>> const cases{
        {"piston", "facing_direction"}, {"sticky_piston", "facing_direction"},
        {"observer", "facing_direction"}, {"observer", "minecraft:facing_direction"},
        {"dispenser", "facing_direction"}, {"dropper", "facing_direction"},
        {"hopper", "facing_direction"}, {"crafter", "orientation"},
        {"lever", "lever_direction"}, {"stone_button", "facing_direction"},
        {"oak_fence_gate", "minecraft:cardinal_direction"},
        {"chest", "minecraft:cardinal_direction"}, {"furnace", "minecraft:cardinal_direction"},
        {"barrel", "facing_direction"}, {"lectern", "direction"},
        {"anvil", "minecraft:cardinal_direction"}, {"white_glazed_terracotta", "facing_direction"},
        {"ladder", "facing_direction"}, {"oak_wall_sign", "facing_direction"},
        {"oak_standing_sign", "ground_sign_direction"}, {"oak_hanging_sign", "ground_sign_direction"},
        {"oak_hanging_sign", "hanging"}, {"oak_hanging_sign", "attached_bit"},
        {"bell", "attachment"}, {"bell", "direction"}, {"amethyst_cluster", "minecraft:block_face"},
        {"rail", "rail_direction"}, {"pointed_dripstone", "vertical_direction"},
        {"bed", "head_piece_bit"}, {"bed", "direction"}
    };
    for (bool manual : {false, true}) {
        gState.manual = manual; // false is the shared easy/range comparison path.
        for (bool native : {false, true}) {
            for (auto const& [name, key] : cases) {
                Block expected{"minecraft:"+name, {{key, "0"}}};
                expected.type.nativeAllows = native;
                auto predicted = expected;
                check("same orientation accepted", placementPredictionMatches(predicted, expected));
                predicted.serialized.mTags[key] = "1";
                nativeCalls = 0;
                check("wrong orientation rejected", !placementPredictionMatches(predicted, expected));
                check("orientation veto precedes native", nativeCalls == 0);
                predicted.serialized.mTags.clear();
                check("missing predicted key rejected", !placementPredictionMatches(predicted, expected));
                check("missing expected key rejected", !placementPredictionMatches(expected, predicted));
                predicted = expected;
                predicted.name = "minecraft:stone";
                check("wrong block identity rejected", !placementPredictionMatches(predicted, expected));
                predicted = expected;
                predicted.missingSerialization = true;
                check("missing actual serialization rejected", !placementPredictionMatches(predicted, expected));
                check("missing expected serialization rejected", !placementPredictionMatches(expected, predicted));
            }
        }
    }
}
void specializedCases() {
    std::vector<Block> const guarded{
        {"minecraft:oak_stairs", {{"weirdo_direction", "0"}, {"upside_down_bit", "0"}}},
        {"minecraft:oak_slab", {{"minecraft:vertical_half", "bottom"}}},
        {"minecraft:stone_slab", {{"top_slot_bit", "0"}}},
        {"minecraft:torch", {{"torch_facing_direction", "top"}}},
        {"minecraft:oak_log", {{"pillar_axis", "x"}}},
        {"minecraft:unpowered_repeater", {{"minecraft:cardinal_direction", "north"}}},
        {"minecraft:unpowered_comparator", {{"minecraft:cardinal_direction", "north"}}}
    };
    for (bool manual : {false, true}) {
        gState.manual = manual;
        for (auto expected : guarded) {
            expected.type.slab = expected.name.ends_with("_slab");
            expected.type.nativeAllows = true;
            check("specialized correct state", placementPredictionMatches(expected, expected));
            for (auto const& [key, value] : expected.serialized.mTags) {
                (void)value;
                auto predicted = expected;
                predicted.serialized.mTags[key] = "different";
                check("specialized orientation protected", !placementPredictionMatches(predicted, expected));
            }
        }
        for (auto name : {"minecraft:trapdoor", "minecraft:oak_trapdoor", "minecraft:iron_trapdoor"}) {
            for (int direction = 0; direction < 4; ++direction) {
                for (int half = 0; half < 2; ++half) {
                    Block expected{name, {{"direction", std::to_string(direction)},
                        {"upside_down_bit", std::to_string(half)}, {"open_bit", "0"}}};
                    expected.type.nativeAllows = true;
                    check("trapdoor exact", placementPredictionMatches(expected, expected));
                    auto predicted = expected;
                    predicted.serialized.mTags["direction"] = std::to_string((direction+1)%4);
                    check("trapdoor direction", !placementPredictionMatches(predicted, expected));
                    predicted = expected;
                    predicted.serialized.mTags["upside_down_bit"] = std::to_string(1-half);
                    check("trapdoor half", !placementPredictionMatches(predicted, expected));
                    predicted = expected;
                    predicted.serialized.mTags["open_bit"] = "1";
                    check("trapdoor open policy", placementPredictionMatches(predicted, expected) == manual);
                }
            }
        }
    }
}
void doorCases() {
    for (bool manual : {false, true}) {
        gState.manual = manual;
        for (auto key : {"direction", "minecraft:cardinal_direction"}) {
            Block expected{"minecraft:oak_door", {{key, std::string(key)=="direction" ? "0" : "north"},
                {"upper_block_bit", "0"}, {"door_hinge_bit", "0"}, {"open_bit", "0"}}};
            expected.door = true;
            expected.type.nativeAllows = true;
            auto predicted = expected;
            check(std::string(key)=="direction" ? "legacy door accepted" : "modern door accepted",
                  placementPredictionMatches(predicted, expected));
            predicted.serialized.mTags[key] = "other";
            check("door wrong direction", !placementPredictionMatches(predicted, expected));
            predicted = expected;
            predicted.serialized.mTags["open_bit"] = "1";
            check("door manual open policy", placementPredictionMatches(predicted, expected) == manual);
            predicted = expected;
            predicted.serialized.mTags["upper_block_bit"] = "1";
            check("door wrong half", !placementPredictionMatches(predicted, expected));
            Block upper = expected;
            upper.serialized.mTags["upper_block_bit"] = "1";
            upper.serialized.mTags["door_hinge_bit"] = "1";
            check("visible door hinge mismatch", !placementPredictionMatches(expected, expected, &upper));
            predicted = expected;
            predicted.serialized.mTags["door_hinge_bit"] = "1";
            check("visible door hinge match", placementPredictionMatches(predicted, expected, &upper));
            check("hidden door hinge remains best effort", placementPredictionMatches(expected, expected));
            // Force runtime inequality with the non-owning lower hinge field.
            check(std::string(key)=="direction" ? "legacy correction" : "modern correction",
                  projectionStatesMatch(expected, predicted));
            predicted.serialized.mTags[key] = "other";
            check("correction rejects wrong direction", !projectionStatesMatch(expected, predicted));
            predicted = expected; predicted.serialized.mTags["open_bit"] = "1";
            check("correction retains open state", !projectionStatesMatch(expected, predicted));
            predicted = upper; predicted.serialized.mTags[key] = "other";
            check("upper correction owns hinge not duplicate direction", projectionStatesMatch(upper, predicted));
            predicted.serialized.mTags["door_hinge_bit"] = "0";
            check("upper correction hinge mismatch", !projectionStatesMatch(upper, predicted));
        }
    }
    Block legacy{"minecraft:oak_door", {{"direction", "0"}, {"upper_block_bit", "0"}, {"open_bit", "0"}}};
    legacy.door = true;
    auto modern = legacy;
    modern.serialized.mTags.erase("direction");
    modern.serialized.mTags["minecraft:cardinal_direction"] = "north";
    check("cross-schema guesses refused", !placementPredictionMatches(modern, legacy));
    auto missing = legacy; missing.serialized.mTags.erase("direction");
    check("door missing direction refused", !placementPredictionMatches(missing, missing));
    auto extra = legacy; extra.serialized.mTags["minecraft:cardinal_direction"] = "north";
    check("extra direction key refused", !placementPredictionMatches(legacy, extra));
}
void compatibilityCases() {
    gState.manual = true;
    std::vector<std::pair<std::string, std::string>> const derived{
        {"observer", "powered_bit"}, {"hopper", "toggle_bit"}, {"stone_button", "button_pressed_bit"},
        {"lever", "open_bit"}, {"oak_fence_gate", "in_wall_bit"}, {"oak_fence_gate", "open_bit"},
        {"cobblestone_wall", "wall_connection_type_north"}, {"tripwire", "attached_bit"},
        {"scaffolding", "stability"}, {"golden_rail", "rail_data_bit"}
    };
    for (auto const& [name, key] : derived) {
        Block expected{"minecraft:"+name, {{"facing_direction", "2"}, {key, "0"}}};
        auto predicted = expected;
        predicted.serialized.mTags[key] = "1";
        check("manual environmental tolerance preserved", placementPredictionMatches(predicted, expected));
        predicted.serialized.mTags["facing_direction"] = "3";
        check("derived state cannot hide wrong facing", !placementPredictionMatches(predicted, expected));
    }
    Block fence{"minecraft:oak_fence", {}};
    auto predicted = fence; predicted.runtimeVariant = 1;
    check("flattened connection tolerance preserved", placementPredictionMatches(predicted, fence));
    Block unknown{"addon:custom", {{"unknown_state", "0"}}};
    predicted = unknown; predicted.serialized.mTags["unknown_state"] = "1";
    check("unknown state not newly ignored", !placementPredictionMatches(predicted, unknown));
    Block furnace{"minecraft:lit_furnace", {{"minecraft:cardinal_direction", "north"}}};
    predicted = furnace; predicted.name = "minecraft:furnace";
    check("runtime alias preserved", placementPredictionMatches(predicted, furnace));
    predicted.serialized.mTags["minecraft:cardinal_direction"] = "south";
    check("alias must preserve direction", !placementPredictionMatches(predicted, furnace));
    // Policy compares typed tags, not their string representation.
    using TypedStates = std::map<std::string, std::variant<int, std::string>>;
    TypedStates expected{{"direction", 0}}, actual{{"direction", std::string{"0"}}};
    check("tag type remains significant", !lholo::block::placementOrientationStatesMatch("minecraft:test", expected, actual));
    check("no horizontal key is not a match", !lholo::block::horizontalDirectionStatesMatch(States{}, States{}));
}
}
int main() {
    genericCases(); specializedCases(); doorCases(); compatibilityCases();
    std::printf("SUMMARY %d checks, %d failures (engine doubles)\n", checks, failures);
    return failures ? 1 : 0;
}
