// LHolo - R11 animated client-only bubble column projection pass.
// The vanilla bubble body is driven by particles, not terrain tessellation.
// This pass draws bounded, camera-facing ghost bubbles without modifying
// real-world particle systems, worlds or block state.
#include "projection/mesh/ProjectionAnimatedBubblePass.h"

#include "projection/core/AnimatedBubbleGeometry.h"
#include "projection/core/VanillaBubbleSprite.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "resourcepacks/WorldPackReloadState.h"
#include "render/OverlayMaterials.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>
#include <glm/vec2.hpp>

#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"

namespace lholo::projection::detail {
namespace {
std::atomic_bool gAnimatedBubbleR11Logged{};
std::atomic_bool gAnimatedBubbleR11AtlasWarning{};
std::atomic<std::int64_t> gAnimatedBubbleR11SummaryMs{};

OffscreenCaptureDescription const& emptyBubbleOffscreenCapture() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

bool dragDownForBubble(Block const& block) {
    for (auto const& [name,tag] : block.mSerializationId.get()) {
        if (name != "states" || !tag.hold<::CompoundTag>()) continue;
        for (auto const& [key,state] : tag.get<::CompoundTag>()) {
            if (key == "drag_down" && state.getId() == ::Tag::Type::Byte)
                return state.get<::ByteTag>().data != 0;
        }
    }
    return false;
}

void advanceAnimatedBubbleCache(ProjectionState& state) {
    if (!state.structure || state.cachedMirror < 0 || state.cachedRotation < 0)
        return;
    auto const* source = state.structure.get();
    if (source != state.animatedBubbleSource
        || state.animatedBubbleGeneration != state.structureGeneration
        || state.animatedBubbleMirror != state.cachedMirror
        || state.animatedBubbleRotation != state.cachedRotation) {
        state.animatedBubbleCells.clear();
        state.animatedBubbleScanCursor = 0;
        state.animatedBubbleSource = source;
        state.animatedBubbleGeneration = state.structureGeneration;
        state.animatedBubbleMirror = state.cachedMirror;
        state.animatedBubbleRotation = state.cachedRotation;
    }
    auto const& blocks = source->renderBlocks;
    auto visited = std::size_t{};
    while (state.animatedBubbleScanCursor < blocks.size()
        && visited < AnimatedBubbleMaxCellsScannedPerFrame
        && state.animatedBubbleCells.size() < AnimatedBubbleMaxCachedCells) {
        auto const index = state.animatedBubbleScanCursor++;
        ++visited;
        auto const& entry = blocks[index];
        if (!entry.block || entry.block->getTypeName() != "minecraft:bubble_column")
            continue;
        auto const transformed = transformStructurePosition(
            entry,*source,state.cachedMirror,state.cachedRotation
        );
        state.animatedBubbleCells.push_back({
            index,transformed,dragDownForBubble(*entry.block)
        });
    }
}

struct VisibleBubble {
    ProjectionAnimatedBubbleCell const* cell{};
    float distanceSquared{};
};

} // namespace

void submitAnimatedBubbleOverlay(
    ProjectionState& state,
    BaseActorRenderContext& renderContext,
    BlockPos const& renderOrigin,
    Vec3 const& cameraWorld,
    float structureOpacity,
    mce::MaterialPtr const& projectedBlendMaterial
) {
    if (!state.structure || !state.terrainTexture
        || projectedBlendMaterial.mRenderMaterialInfoPtr.get() == nullptr
        || !std::isfinite(structureOpacity) || structureOpacity <= .003F)
        return;
    advanceAnimatedBubbleCache(state);
    if (state.animatedBubbleCells.empty()) return;

    // The vanilla particle texture is resolved per render submission. Never
    // cache a texture pointer across world/resource-pack reloads. When the
    // actual particle atlas is unavailable, R11's proven procedural rings
    // remain as an explicit nonvanilla fallback rather than hiding the effect.
    std::optional<mce::TexturePtr> vanillaParticleAtlas;
    if (state.client) {
        if (auto* levelRenderer = state.client->getLevelRenderer()) {
            auto const& textureGroup = levelRenderer->mTextureGroup.get();
            if (textureGroup) {
                auto particleTexture = textureGroup->getTexture(
                    ResourceLocation{Core::PathView{"textures/particle/particles"}},
                    false, std::nullopt, cg::TextureSetLayerType::Color
                );
                if (particleTexture.mClientTexture
                    && particleTexture.mClientTexture->mIsMissingTexture
                        != IsMissingTexture::Yes) {
                    vanillaParticleAtlas.emplace(std::move(particleTexture));
                }
            }
        }
    }
    auto const bubbleWhite = Block::tryGetFromRegistry(
        HashedString("minecraft:white_concrete")
    );
    auto const* graphics = bubbleWhite ? BlockGraphics::getForBlock(*bubbleWhite) : nullptr;
    if (!graphics) {
        if (!gAnimatedBubbleR11AtlasWarning.exchange(true, std::memory_order_acq_rel))
            LHolo::getInstance().getSelf().getLogger().warn(
                "LHoloBubbleR11 enabled=0 reason=white_concrete_atlas_unavailable"
            );
        return;
    }
    auto const& atlas = graphics->getTexture(0,0);
    if (!std::isfinite(atlas._u0) || !std::isfinite(atlas._v0)
        || !std::isfinite(atlas._u1) || !std::isfinite(atlas._v1)
        || atlas._u1 <= atlas._u0 || atlas._v1 <= atlas._v0) return;
    float const u = (atlas._u0+atlas._u1)*.5F;
    float const v = (atlas._v0+atlas._v1)*.5F;

    AnimatedBubbleVec3 const localCamera{
        cameraWorld.x - static_cast<float>(renderOrigin.x),
        cameraWorld.y - static_cast<float>(renderOrigin.y),
        cameraWorld.z - static_cast<float>(renderOrigin.z)
    };
    auto const axis = state.cachedLayerAxis.value_or(structure::LayerAxis::Y);
    auto const mode = state.cachedLayerDisplayMode.value_or(structure::LayerDisplayMode::All);
    std::vector<VisibleBubble> visible;
    visible.reserve(std::min(state.animatedBubbleCells.size(),AnimatedBubbleMaxCachedCells));
    for (auto const& cell : state.animatedBubbleCells) {
        auto const index = cell.index;
        if (index >= state.structure->renderBlocks.size()
            || index >= state.correctionStates.size()
            || index >= state.missingLayers.size()
            || !projectionBodyShouldRender(state.correctionStates[index],
                state.missingLayers[index]))
            continue;
        auto const& entry = state.structure->renderBlocks[index];
        if (!isLayerVisible(
                projectionLayer(*state.structure,entry,axis,
                    state.cachedMirror,state.cachedRotation),
                mode,state.cachedDisplayLayer,entry.materialIndex,
                entry.liquidMaterialIndex,axis))
            continue;
        float const dx = localCamera.x-static_cast<float>(cell.local.x)-.5F;
        float const dy = localCamera.y-static_cast<float>(cell.local.y)-.5F;
        float const dz = localCamera.z-static_cast<float>(cell.local.z)-.5F;
        auto const distance = dx*dx+dy*dy+dz*dz;
        if (!std::isfinite(distance) || distance
            > AnimatedBubbleViewRadius*AnimatedBubbleViewRadius) continue;
        visible.push_back({&cell,distance});
    }
    if (visible.empty()) return;

    // Max 48 cells × 4 small billboard bubbles. Expensive structures do not
    // allocate or submit thousands of real particles every render frame.
    auto const nearest = std::min(visible.size(),AnimatedBubbleMaxVisibleCells);
    std::partial_sort(visible.begin(),visible.begin()+nearest,visible.end(),
        [](VisibleBubble const& a,VisibleBubble const& b) {
            return a.distanceSquared < b.distanceSquared;
        });
    visible.resize(nearest);
    std::sort(visible.begin(),visible.end(),
        [](VisibleBubble const& a,VisibleBubble const& b) {
            return a.distanceSquared > b.distanceSquared;
        });

    // An independent tessellator is required: the shared ScreenContext
    // tessellator is reused for later UI / item rendering in the same frame.
    Tessellator t{renderContext.mScreenContext.tessellator.mBufferResourceService};
    t.begin(
        Tessellator::DebugContextCallback{},
        mce::PrimitiveMode::QuadList,
        static_cast<int>(visible.size()*AnimatedBubbleVerticesPerCell),
        true
    );
    auto const seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    std::size_t submittedQuads{};
    auto const alpha = std::clamp(structureOpacity,0.F,1.F);
    for (auto const& candidate : visible) {
        auto const& cell = *candidate.cell;
        if (vanillaParticleAtlas) {
            emitVanillaBubbleSprites(
                {static_cast<float>(cell.local.x),
                 static_cast<float>(cell.local.y),
                 static_cast<float>(cell.local.z)},
                localCamera,cell.dragDown,seconds,
                static_cast<std::uint32_t>(cell.index),
                [&](VanillaBubbleSprite const& sprite) {
                    // Vanilla Bedrock 1.26.51.01 particle atlas: 128x128.
                    // Both bubble_column_up/down MCB definitions sample the
                    // same 8x8 sprite with UV start=(0,16).
                    constexpr std::array<glm::vec2,4> uv{{
                        {VanillaBubbleU0,VanillaBubbleV0},
                        {VanillaBubbleU1,VanillaBubbleV0},
                        {VanillaBubbleU1,VanillaBubbleV1},
                        {VanillaBubbleU0,VanillaBubbleV1}
                    }};
                    t.color(1.F,1.F,1.F,sprite.opacity*alpha);
                    for (std::size_t i=0;i<4U;++i) {
                        t.tex2({uv[i].x,uv[i].y});
                        auto const& point=sprite.vertices[i];
                        t.vertex(point.x,point.y,point.z);
                    }
                    ++submittedQuads;
                }
            );
        } else {
            // R11 accepted safe fallback for a missing/reloading atlas.
            emitAnimatedBubbleCell(
                {static_cast<float>(cell.local.x),
                 static_cast<float>(cell.local.y),
                 static_cast<float>(cell.local.z)},
                localCamera,cell.dragDown,seconds,
                static_cast<std::uint32_t>(cell.index),
                [&](AnimatedBubbleBillboardQuad const& quad) {
                    t.color(.82F,.94F,1.F,quad.opacity*alpha);
                    for (auto const& point : quad.corners) {
                        t.tex2({u,v});
                        t.vertex(point.x,point.y,point.z);
                    }
                    ++submittedQuads;
                }
            );
        }
    }
    if (!submittedQuads) {
        t.end(Tessellator::UploadMode::Never,
            "LHoloBubbleR11Empty",SupplementaryFieldAutoGenerationMode{0});
        return;
    }
    resourcepacks::state().submitted(resourcepacks::Domain::Projection);
    if (vanillaParticleAtlas) {
        MeshHelpers::renderMeshImmediately(
            renderContext.mScreenContext,t,projectedBlendMaterial,
            {std::cref(*vanillaParticleAtlas)},emptyBubbleOffscreenCapture()
        );
    } else {
        MeshHelpers::renderMeshImmediately(
            renderContext.mScreenContext,t,projectedBlendMaterial,
            {std::cref(*state.terrainTexture)},emptyBubbleOffscreenCapture()
        );
    }

    auto& logger=LHolo::getInstance().getSelf().getLogger();
    static std::atomic_bool r12TextureLogged{};
    if (!r12TextureLogged.exchange(true,std::memory_order_acq_rel)) {
        logger.info(
            "LHoloBubbleR12 nativeParticleAtlas={} mode={} "
            "spriteUv=(0,16,8,8)/128 spritesPerCell={} "
            "fullNativeEmitter=0 virtualWorldAirSafe=1",
            vanillaParticleAtlas ? 1 : 0,
            vanillaParticleAtlas ? "vanilla_bedrock_texture" : "R11_fallback",
            AnimatedBubblePerCell
        );
    }
    if (!gAnimatedBubbleR11Logged.exchange(true,std::memory_order_acq_rel))
        logger.info(
            "LHoloBubbleR11 animatedBillboards=1 upDown=1 cameraFacing=1 "
            "particleTexture=terrain_white_sample spriteQuads={} maxCells={} "
            "spawnedWorldParticles=0 perFrameBounded=1",
            submittedQuads,AnimatedBubbleMaxVisibleCells
        );
    auto const nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto prev=gAnimatedBubbleR11SummaryMs.load(std::memory_order_acquire);
    if (nowMs-prev>=5000 && gAnimatedBubbleR11SummaryMs.compare_exchange_strong(
        prev,nowMs,std::memory_order_acq_rel))
        logger.info(
            "LHoloBubbleR11 cellsVisible={} sourceCached={} sourceScanned={} "
            "billboardQuads={} sourceCount={} maxVisibleCells={} activeOpacity={}",
            visible.size(),state.animatedBubbleCells.size(),
            state.animatedBubbleScanCursor,submittedQuads,
            state.structure->renderBlocks.size(),AnimatedBubbleMaxVisibleCells,
            structureOpacity
        );
}
} // namespace lholo::projection::detail
