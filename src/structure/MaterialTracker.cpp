// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/MaterialTracker.h"
#include "io/MaterialExport.h"
#include "structure/InventoryContents.h"
#include "structure/SchematicRuntime.h"
#include "structure/PlacementTransform.h"
#include "structure/Verification.h"
#include "mc/world/level/block/VanillaStates.h"
#include "app/FutureResult.h"
#include "app/NativeCallbackBoundary.h"

#include "block/BlockPlacementRules.h"
#include "projection/Projection.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/locale/I18n.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/ItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/level/block/Block.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lholo::structure::detail {
namespace {

constexpr std::uint64_t kAvailabilityRefreshMs = 400;
constexpr std::uint64_t kMaterialHudRecountIntervalMs = 400;
std::atomic_bool gMaterialListInvalidated{};

using BlockCounts = std::unordered_map<Block const*, std::uint64_t>;

struct RawMaterialCounts {
    BlockCounts body;
    BlockCounts liquid;
};

using MaterialHudKey = projection::MaterialProgressKey;
using MaterialHudInput = projection::MaterialProgressSnapshot;

struct MaterialHudResult {
    MaterialHudKey   key;
    RawMaterialCounts counts;
};

struct MaterialHudWorkerState {
    std::optional<std::future<MaterialHudResult>> inFlight;
    std::optional<MaterialHudKey>                 published;
    std::uint64_t                                 nextScheduleAt{};
};

MaterialHudWorkerState& materialHudWorkerState() {
    static MaterialHudWorkerState state;
    return state;
}

void countBlock(BlockCounts& counts, Block const* blockValue) {
    if (!blockValue) return;
    auto& count = counts[blockValue];
    if (count != std::numeric_limits<std::uint64_t>::max()) ++count;
}

RawMaterialCounts collectRawMaterials(
    std::vector<LoadedStructure::RenderBlock> const& renderBlocks
) {
    RawMaterialCounts counts;
    // Palette cardinality is normally tiny compared with the block count. Do
    // not reserve one hash bucket per projected cell for very large structures.
    counts.body.reserve(std::min<std::size_t>(renderBlocks.size(), 4096));
    counts.liquid.reserve(std::min<std::size_t>(renderBlocks.size(), 64));
    for (auto const& entry : renderBlocks) {
        countBlock(counts.body, entry.block);
        countBlock(counts.liquid, entry.liquid);
    }
    return counts;
}

std::string localizedBlockName(Block const& block, std::string_view localeCode) {
    auto const& typeName = block.getTypeName();
    auto const itemId = block.getBlockItemId();
    auto const item = ItemRegistryManager::getItemRegistry().getItem(itemId);
    if (auto* itemPtr = item.get()) {
        ItemStack itemStack;
        itemStack.reinit(*itemPtr, 1, 0);
        auto const name = itemStack.getName();
        if (!name.empty() && name != typeName) return name;
    }

    auto const translationKey = block.buildDescriptionName();
    if (!translationKey.empty()) {
        auto& i18n = ::getI18n();
        auto locale = localeCode.empty()
            ? i18n.getCurrentLanguage().get()
            : i18n.getLocaleFor(std::string{localeCode});
        if (locale) {
            auto const localized = i18n.get(translationKey, std::vector<std::string>{}, locale);
            if (!localized.empty() && localized != translationKey) return localized;
        }
    }

    auto name = block.getDisplayName();
    if (name.empty()) name = typeName;
    return name;
}

std::vector<MaterialRequirement> resolveMaterials(
    RawMaterialCounts counts,
    std::string_view localeCode
) {
    std::map<std::string, MaterialRequirement> byType;
    std::map<std::string, MaterialRequirement> byLiquidType;
    auto aggregate = [&](auto& destination, Block const* blockValue, std::uint64_t blockCount, bool liquid=false) {
        if (!blockValue || blockCount == 0) return;

        std::string const typeName{blockValue->getTypeName()};
        auto const rule = requiredItemRule(typeName,
            blockValue->getState<bool>(VanillaStates::UpperBlockBit()).value_or(false),
            blockValue->getState<bool>(VanillaStates::HeadPieceBit()).value_or(false));
        if (!rule.quantity) return;
        blockCount = blockCount > std::numeric_limits<std::uint64_t>::max()/rule.quantity
            ? std::numeric_limits<std::uint64_t>::max() : blockCount * rule.quantity;
        auto const key = block::materialKey(typeName);
        if (key.empty()) return;
        MaterialRequirement requirement;
        requirement.typeName = typeName;
        if (typeName == "minecraft:water" || typeName == "minecraft:flowing_water") {
            requirement.nameKey = i18n::TextKey::MaterialWater;
        } else if (typeName == "minecraft:lava" || typeName == "minecraft:flowing_lava") {
            requirement.nameKey = i18n::TextKey::MaterialLava;
        } else if (auto const item = block::resolvePlacementItem(*blockValue); item.valid) {
            requirement.displayName = item.displayName;
            requirement.itemId = item.itemId;
            requirement.stackSize = item.stackSize;
        } else {
            requirement.displayName = localizedBlockName(*blockValue, localeCode);
        }

        requirement.key = std::string(liquid ? "liquid:" : "body:") + key + "|item=" + requirement.itemId;
        auto const result = destination.try_emplace(key, std::move(requirement));
        auto& total = result.first->second.count;
        auto const maximum = std::numeric_limits<std::uint64_t>::max();
        total = blockCount > maximum - total ? maximum : total + blockCount;
    };

    // Registry/localization work now runs once per unique palette state rather
    // than once per projected cell. This is the critical path for million-block
    // structures; the first pass above is only pointer counting.
    for (auto const& [blockValue, count] : counts.body) aggregate(byType, blockValue, count);
    for (auto const& [blockValue, count] : counts.liquid) {
        aggregate(byLiquidType, blockValue, count, true);
    }

    std::vector<MaterialRequirement> materials;
    materials.reserve(byType.size() + byLiquidType.size());
    auto appendSorted = [&materials](auto& source) {
        std::vector<std::pair<std::string, MaterialRequirement>> sorted;
        sorted.reserve(source.size());
        for (auto& [key, requirement] : source) {
            sorted.emplace_back(key, std::move(requirement));
        }
        std::sort(sorted.begin(), sorted.end(), [](auto const& left, auto const& right) {
            if (left.second.count != right.second.count) {
                return left.second.count > right.second.count;
            }
            return left.first < right.first;
        });
        for (auto& [key, requirement] : sorted) {
            (void)key;
            materials.push_back(std::move(requirement));
        }
    };
    appendSorted(byType);
    appendSorted(byLiquidType);
    return materials;
}

std::vector<MaterialRequirement> collectMaterials(
    std::vector<LoadedStructure::RenderBlock> const& renderBlocks,
    std::string_view localeCode
) {
    return resolveMaterials(collectRawMaterials(renderBlocks), localeCode);
}

std::optional<MaterialHudKey> currentMaterialHudKey() {
    auto key = projection::getMaterialProgressKey();
    auto const loaded = StructureSession::getInstance().loaded();
    // Present can replace the requested structure before the next opaque frame
    // rebuilds ProjectionState. Its previous key must not label the new HUD.
    if (!loaded || !key || loaded->generation != key->structureGeneration) return std::nullopt;
    return key;
}

std::optional<MaterialHudInput> captureMaterialHudInput(MaterialHudKey const& key) {
    return projection::captureMaterialProgress(key);
}

MaterialHudResult countMaterialHud(MaterialHudInput input) {
    MaterialHudResult result;
    result.key = input.key;
    auto const& blocks = input.structure->renderBlocks;
    result.counts.body.reserve(std::min<std::size_t>(blocks.size(), 4096));
    result.counts.liquid.reserve(std::min<std::size_t>(blocks.size(), 64));
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (input.progressCorrect[index] != 0) continue;
        auto const& entry = blocks[index];
        auto const layer = layerOf({{input.structure->sizeX,input.structure->sizeY,input.structure->sizeZ},{},input.key.rotation,input.key.mirror},
            {entry.x,entry.y,entry.z}, input.key.layerAxis);
        if (!projection::isLayerVisible(
            layer, input.key.layerDisplayMode, input.key.displayLayer,
                entry.materialIndex, entry.liquidMaterialIndex, input.key.layerAxis
            )) {
            continue;
        }
        // Block pointers are opaque keys here. Registry, localization and item
        // resolution remain on the game tick thread after aggregation.
        countBlock(result.counts.body, entry.block);
        countBlock(result.counts.liquid, entry.liquid);
    }
    return result;
}

std::vector<int> collectInventoryAvailability(
    LocalPlayer&                            player,
    std::vector<MaterialRequirement> const& requirements
) {
    auto inventoryCounts = countInventoryItems(player.getInventory());

    std::vector<int> available(requirements.size(), 0);
    for (std::size_t index = 0; index < requirements.size(); ++index) {
        auto const& itemId = requirements[index].itemId;
        if (itemId.empty()) continue;
        if (auto const found = inventoryCounts.find(itemId); found != inventoryCounts.end()) {
            available[index] = found->second;
        }
    }
    return available;
}

void updateMaterialHud(LocalPlayer& player) {
    auto& ui = StructureUiState::getInstance();
    auto& worker = materialHudWorkerState();
    if (!ui.materialHudEnabled()) return;

    if (worker.inFlight
        && worker.inFlight->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
        // Retry a failed task at the existing cadence; the native boundary
        // reports the original error after this consumed slot has been retired.
        worker.nextScheduleAt = GetTickCount64() + kMaterialHudRecountIntervalMs;
        auto result = app::takeFutureResult(worker.inFlight);
        auto const publicationRevision = ui.materialHudRevision();
        if (auto const current = currentMaterialHudKey(); current && *current == result.key) {
            auto materials = resolveMaterials(
                std::move(result.counts), player.getLocaleCode()
            );
            auto available = collectInventoryAvailability(player, materials);
            // Publish both vectors under one lock. The render thread therefore
            // sees either the complete old snapshot or the complete new one.
            if (ui.replaceMaterialHudSnapshot(
                    std::move(materials), std::move(available), publicationRevision)) {
                worker.published = result.key;
            }
        }
    }

    auto const key = currentMaterialHudKey();
    if (!key) {
        worker.published.reset();
        return;
    }
    if (worker.inFlight || (worker.published && *worker.published == *key)) return;
    auto const now = GetTickCount64();
    if (now < worker.nextScheduleAt) return;

    worker.nextScheduleAt = now + kMaterialHudRecountIntervalMs;
    auto input = captureMaterialHudInput(*key);
    if (!input) return;

    worker.inFlight.emplace(std::async(
        std::launch::async,
        [input = std::move(*input)]() mutable { return countMaterialHud(std::move(input)); }
    ));
}

void processPendingMaterialList(LocalPlayer& player) {
    auto& ui = StructureUiState::getInstance();
    if (!ui.consumeMaterialListRequest()) return;

    auto const token=ui.materialListToken();
    auto const session=StructureSession::getInstance().snapshot();
    auto const loaded=session.loaded;
    std::vector<MaterialRequirement> materials;
    if (loaded) materials = collectMaterials(loaded->renderBlocks, player.getLocaleCode());
    // Loading another structure can overlap this game-thread calculation. Never
    // publish a completed list for a structure that is no longer active.
    if (StructureSession::getInstance().loaded() != loaded) {
        ui.requestMaterialList();
        return;
    }
    if (!loaded) return;
    bool inventoryComplete{};
    auto const inventory=countInventoryItems(player.getInventory(),&inventoryComplete);
    std::vector<std::optional<int>> availability;
    availability.reserve(materials.size());
    for(auto const& row:materials) {
        if(!inventoryComplete || row.itemId.empty())availability.push_back(std::nullopt);
        else { auto found=inventory.find(row.itemId);availability.push_back(found==inventory.end()?0:found->second); }
    }
    (void)ui.publishMaterialList(std::move(materials),std::move(availability),loaded->generation,session.lastPath,token);
    // Cover the narrow hand-off where the active structure changes between the
    // identity check above and publishing the snapshot.
    if (StructureSession::getInstance().loaded() != loaded) {
        ui.clearMaterials();
        ui.requestMaterialList();
    }
}

void refreshMaterialListAvailability(LocalPlayer& player) {
    static std::uint64_t lastRefresh{};
    auto& ui=StructureUiState::getInstance();auto const snapshot=ui.materialListView();
    if(!snapshot || snapshot->requirements.empty())return;
    auto const loaded=StructureSession::getInstance().loaded();
    if(!loaded || loaded->generation!=snapshot->scope.generation)return;
    auto const force=ui.consumeMaterialAvailabilityRefresh();
    auto const now=GetTickCount64();if(!force && lastRefresh && now-lastRefresh<kAvailabilityRefreshMs)return;
    lastRefresh=now;
    bool inventoryComplete{};
    auto const inventory=countInventoryItems(player.getInventory(),&inventoryComplete);
    std::vector<std::optional<int>> counts;counts.reserve(snapshot->requirements.size());
    for(auto const& row:snapshot->requirements) {
        if(!inventoryComplete || row.itemId.empty())counts.push_back(std::nullopt);
        else { auto found=inventory.find(row.itemId);counts.push_back(found==inventory.end()?0:found->second); }
    }
    if(StructureSession::getInstance().loaded()==loaded)(void)ui.setMaterialListAvailability(snapshot->scope,std::move(counts));
}

void refreshAvailability(LocalPlayer& player) {
    static std::uint64_t lastRefreshMs{};
    auto& ui = StructureUiState::getInstance();
    if (!ui.materialHudEnabled()) return;

    auto const snapshot = ui.materialHudView();
    if (!snapshot) return;
    auto const& requirements = snapshot->requirements;
    if (requirements.empty()) return;
    auto const now = GetTickCount64();
    if (lastRefreshMs != 0 && now - lastRefreshMs < kAvailabilityRefreshMs) return;
    lastRefreshMs = now;

    (void)ui.setMaterialHudAvailability(snapshot->revision, collectInventoryAvailability(player, requirements));
}

} // namespace

void requestMaterialListRefresh() {
    StructureUiState::getInstance().requestMaterialList();
}

void invalidateMaterialList() {
    auto& ui = StructureUiState::getInstance();
    ui.clearMaterials();
    // Invalidation can come from Present or a world/render transition. The
    // future/published key stay game-tick-owned; only this request crosses threads.
    gMaterialListInvalidated.store(true, std::memory_order_release);
}

void tickMaterialTracker(LocalPlayer& player) {
    if (gMaterialListInvalidated.exchange(false, std::memory_order_acq_rel)) {
        auto& worker = materialHudWorkerState();
        worker.published.reset();
        worker.nextScheduleAt = 0;
    }
    processPendingMaterialList(player);
    refreshMaterialListAvailability(player);
    updateMaterialHud(player);
    refreshAvailability(player);
}

void shutdownMaterialTracker() {
    io::materialExportJob().shutdown();
    schematic::shutdown();
    auto& worker = materialHudWorkerState();
    if (worker.inFlight) {
        auto inFlight = app::takePendingValue(worker.inFlight);
        // The task owns only immutable structure data and performs finite CPU
        // work. Join before the DLL unloads so no worker can execute old code.
        app::invokeNativeCallback([&] { (void)inFlight.get(); }, [](char const* reason) noexcept {
            app::reportNativeCallbackFailure("material tracker shutdown", reason);
        });
    }
    worker.published.reset();
    worker.nextScheduleAt = 0;
    gMaterialListInvalidated.store(false, std::memory_order_release);
    StructureUiState::getInstance().clearMaterialHud();
}

} // namespace lholo::structure::detail
