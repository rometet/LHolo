// SPDX-License-Identifier: GPL-3.0-or-later
#include "MockBedrock.h"
#include "block/BlockPlacementRules.h"
#include <iostream>
#include <stdexcept>
int checks{};
void check(bool value) { ++checks; if (!value) throw std::runtime_error("check " + std::to_string(checks)); }
int main() {
    try {
        using lholo::block::makePlacementItem;
        auto stone = makePlacementItem(Block{"minecraft:stone", "test:variant_item", 7, {}});
        check(stone.getTypeName() == "minecraft:stone");
        check(stone.getAuxValue() == 0);
        check(mock::nativeItemConversions == 0); // preserve existing successful neutral-name path
        for (auto const& [block, item] : {
            std::pair{"minecraft:wall_sign", "minecraft:oak_sign"},
            std::pair{"minecraft:coral_fan_hang", "minecraft:horn_coral_fan"},
            // Synthetic name/aux fixture: not a claim about a live game's ids.
            std::pair{"test:connection_variant", "test:variant_item"}
        }) {
            auto result = makePlacementItem(Block{block, item, 7, {}});
            check(!result.isNull());
            check(result.getTypeName() == item);
            check(result.getAuxValue() == 7);
            check(result.worldState.empty());
        }
        for (auto const& [block, item] : {
            std::pair{"minecraft:powered_repeater", "minecraft:repeater"},
            std::pair{"minecraft:powered_comparator", "minecraft:comparator"},
            std::pair{"minecraft:redstone_wire", "minecraft:redstone"},
            std::pair{"minecraft:lit_furnace", "minecraft:furnace"},
            std::pair{"minecraft:lit_redstone_lamp", "minecraft:redstone_lamp"}
        }) {
            auto result = makePlacementItem(Block{block, "test:variant_item", 12, {}});
            check(result.getTypeName() == item);
            check(result.getAuxValue() == 0);
        }
        check(makePlacementItem(Block{"test:unknown", "", 0, {}}).isNull());
        auto material = lholo::block::resolvePlacementItem(Block{"minecraft:wall_sign", "minecraft:oak_sign", 0, {}});
        check(material.valid && material.itemId == "minecraft:oak_sign");
        std::cout << "PlacementItemTests (mock Bedrock): " << checks << " checks, 0 failures\n";
    } catch (std::exception const& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
