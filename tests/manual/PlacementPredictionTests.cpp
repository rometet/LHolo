// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the actual comparator, connection wrapper and inventory search with
// engine test doubles. This does NOT emulate or validate Minecraft's ABI.
#include <array>
#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

struct Block;
struct BlockSource;
struct BlockPos { int x{}, y{}, z{}; bool operator==(BlockPos const&) const = default; };
enum class BlockProperty { Door };
enum class NeighborDirection { Down, Up, North, South, West, East, Count };
struct NeighborBlockDirections {
    struct Wrapper { std::set<NeighborDirection> value; auto& get() { return value; } } mDirections;
};
inline thread_local int suppressionDepth{};
struct ScopedRegionWriteSuppression {
    ScopedRegionWriteSuppression() { ++suppressionDepth; }
    ~ScopedRegionWriteSuppression() { --suppressionDepth; }
};
struct BlockType {
    bool fence{}, thin{}, slab{}, allowMismatch{};
    bool isFenceBlock() const { return fence; }
    bool isThinFenceBlock() const { return thin; }
    bool isSlabBlock() const { return slab; }
    bool allowStateMismatchOnPlacement(Block const&, Block const&) const { return allowMismatch; }
    Block const& connectionUpdate(BlockSource&, Block const&, BlockPos const&, NeighborBlockDirections const&) const;
};
struct SlabBlock : BlockType { bool mIsDouble{}; };
struct Block {
    std::string name;
    BlockType const* type{};
    std::map<std::string, std::string> states;
    bool door{};
    int itemKey{};
    std::string const& getTypeName() const { return name; }
    BlockType const& getBlockType() const { return *type; }
    bool hasProperty(BlockProperty) const { return door; }
    bool operator==(Block const& other) const { return name == other.name && states == other.states; }
};
struct BlockSource {
    int actualConnections{}, calls{}, attemptedWrites{}, committedWrites{};
    BlockPos expectedCell{-17, 72, 23};
    bool wrongContext{}, incompleteDirections{};
    std::deque<Block> interned;
};
Block const& BlockType::connectionUpdate(BlockSource& region, Block const& block, BlockPos const& pos, NeighborBlockDirections const& directions) const {
    ++region.calls;
    ++region.attemptedWrites;
    if (suppressionDepth == 0) ++region.committedWrites;
    region.wrongContext |= pos != region.expectedCell;
    region.incompleteDirections |= directions.mDirections.value.size() != 6;
    Block result = block;
    result.states["test_derived_connections"] = std::to_string(region.actualConnections);
    region.interned.push_back(std::move(result));
    return region.interned.back();
}
namespace lholo::projection::detail {
#include "connection_function.inc"
}
struct ItemStack {
    int idAux{};
    bool isNull() const { return idAux == 0; }
    int getIdAux() const { return idAux; }
};
struct Inventory {
    std::array<ItemStack, 36> items{};
    ItemStack const& getItem(int slot) const { return items.at(slot); }
};
struct Player { Inventory inventory; auto& getInventory() { return inventory; } };
namespace lholo::block {
// These are dependencies of the comparator, not the code under test here.
std::string_view placeableBaseName(std::string_view name) {
    if (name == "minecraft:powered_repeater") return "minecraft:unpowered_repeater";
    if (name == "minecraft:lit_furnace") return "minecraft:furnace";
    return name;
}
ItemStack makePlacementItem(Block const& block) { return ItemStack{block.itemKey}; }
}
namespace lholo::place {
constexpr int kInventorySlots = 36;
struct ItemFind { int slot; ItemStack const* item; };
std::string serializedState(Block const& block, char const* key) {
    auto const found = block.states.find(key);
    return found == block.states.end() ? std::string{} : found->second;
}
#include "placement_functions.inc"
}

int checks{};
void check(bool condition, char const* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        using namespace lholo::place;
        BlockType ordinary;
        BlockType fence; fence.fence = true;
        BlockType thin; thin.thin = true;
        SlabBlock slab; slab.slab = true;
        BlockSource region;
        auto matches = [&](Block const& predicted, Block const& ghost, bool manual = true, Block const* upper = nullptr) {
            return placementPredictionMatchesInWorld(predicted, ghost, upper, region, region.expectedCell, manual);
        };
        for (auto const* name : {"minecraft:oak_fence", "minecraft:glass_pane", "minecraft:iron_bars", "minecraft:red_stained_glass_pane"}) {
            auto const* type = std::string_view{name}.ends_with("_fence") ? &fence : &thin;
            for (int neighbors = 0; neighbors < 16; ++neighbors) {
                region.actualConnections = neighbors;
                for (int saved = 0; saved < 16; ++saved) {
                    Block ghost{name, type, {{"test_derived_connections", std::to_string(saved)}}, false, 7};
                    for (int predictedMask = 0; predictedMask < 16; ++predictedMask) {
                        Block predicted = ghost;
                        predicted.states["test_derived_connections"] = std::to_string(predictedMask);
                        region.interned.clear();
                        auto const before = region.calls;
                        check(matches(predicted, ghost), "manual connected-block regression");
                        check(region.calls - before == (saved == predictedMask ? 0 : 2), "normalization only on mismatch");
                        check(matches(predicted, ghost, false) == (saved == predictedMask), "automatic behavior unchanged");
                        check(region.calls - before == (saved == predictedMask ? 0 : 2), "auto must not normalize");
                    }
                }
            }
        }
        check(region.committedWrites == 0, "connection wrapper must suppress writes");
        check(region.attemptedWrites == region.calls && region.calls > 0, "write suppression path exercised");
        check(!region.wrongContext && !region.incompleteDirections, "same cell and all neighbor directions");
        check(suppressionDepth == 0, "no leaked suppression scope");
        Block ghost{"minecraft:oak_fence", &fence, {{"test_derived_connections", "15"}, {"custom_intrinsic_state", "a"}}, false, 7};
        Block wrong = ghost; wrong.name = "minecraft:birch_fence";
        auto const before = region.calls;
        check(!matches(wrong, ghost), "wrong block type stays rejected");
        check(region.calls == before, "wrong identity must not normalize");
        wrong = ghost; wrong.states["custom_intrinsic_state"] = "b";
        check(!matches(wrong, ghost), "unknown non-derived state stays strict");
        wrong = ghost; wrong.states.erase("custom_intrinsic_state");
        check(!matches(wrong, ghost), "missing intrinsic state stays strict");
        Block plain{"custom:connectable", &ordinary, {{"custom_state", "a"}}, false, 7};
        wrong = plain; wrong.states["custom_state"] = "b";
        check(!matches(wrong, plain), "unknown family stays strict");
        for (auto const* key : {"weirdo_direction", "upside_down_bit"}) {
            Block stairs{"minecraft:oak_stairs", &ordinary, {{"weirdo_direction", "0"}, {"upside_down_bit", "0"}}, false, 7};
            wrong = stairs; wrong.states[key] = "1";
            check(!matches(wrong, stairs), "stair direction/half retained");
            check(matches(stairs, stairs), "valid stairs retained");
        }
        for (auto const* key : {"minecraft:vertical_half", "top_slot_bit"}) {
            Block half{"minecraft:stone_slab", &slab, {{key, "0"}}, false, 7};
            wrong = half; wrong.states[key] = "1";
            check(!matches(wrong, half), "slab half retained");
            check(matches(half, half), "valid slab retained");
        }
        for (auto const* key : {"pillar_axis", "torch_facing_direction", "facing_direction", "minecraft:cardinal_direction"}) {
            Block directional{"minecraft:directional_test", &ordinary, {{key, "north"}}, false, 7};
            wrong = directional; wrong.states[key] = "south";
            check(!matches(wrong, directional), "directional mismatch retained");
        }
        Block door{"minecraft:oak_door", &ordinary, {{"upper_block_bit", "0"}, {"direction", "0"}, {"open_bit", "0"}, {"door_hinge_bit", "1"}}, true, 7};
        Block upper = door; upper.states["upper_block_bit"] = "1";
        check(matches(door, door, true, &upper), "valid door retained");
        for (auto const* key : {"upper_block_bit", "direction", "open_bit", "door_hinge_bit"}) {
            wrong = door; wrong.states[key] = key == std::string_view{"door_hinge_bit"} ? "0" : "1";
            check(!matches(wrong, door, true, &upper), "door states retained");
        }
        Player player;
        Block unresolvable{"custom:no_item", &ordinary, {}, false, 0};
        check(findItemSlot(player, unresolvable).slot == -1, "unknown material must not match empty hotbar");
        check(findItemSlot(snapshotInventory(player), unresolvable).slot == -1, "unknown material must not match snapshot");
        check(snapshotInventory(player).empty(), "empty slots not indexed");
        check(findItemSlot(player, ghost).slot == -1, "absent item stays missing");
        player.inventory.items[17] = ItemStack{7};
        player.inventory.items[3] = ItemStack{8};
        check(findItemSlot(player, ghost).slot == 17, "real matching item found");
        check(findItemSlot(snapshotInventory(player), ghost).slot == 17, "snapshot finds real matching item");
        check(snapshotInventory(player).size() == 2, "snapshot indexes occupied slots only");
        std::cout << "PlacementPredictionTests: " << checks << " checks PASS (engine doubles)\n";
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
