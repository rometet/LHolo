// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/mesh/ProjectionSectionBuilder.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionLiquidCompatColor.h"
#include "projection/core/ProjectionLiquidFaceCull.h"
#include "projection/core/ProjectionLiquidUv.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionVirtualWorld.h"
#include "plugin/LHolo.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/TessellatorQuadInfo.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/client/world/level/biome/biome_color_sampling/TessellationPolicy.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/world/Facing.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/VanillaBlockTypeIds.h"
#include "mc/world/level/biome/biome_color_sampling/BiomeColorSampling.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"
#include "ll/api/mod/NativeMod.h"

namespace lholo::projection::detail {
namespace {

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

std::atomic_bool gNativeLiquidPositiveLogged{};
std::atomic_bool gNativeLiquidUvRemapLogged{};
std::atomic_bool gNativeLiquidUvFailureLogged{};
std::atomic_bool gNativeLiquidCullLogged{};
std::atomic_bool gNativeLiquidCullSkipLogged{};
std::atomic_bool gPraxisCompatLiquidColorLogged{};
std::atomic_bool gPraxisLiquidColorSeedLogged{};
std::atomic<std::uint32_t> gSubmergedBodyLogCount{};
std::atomic_bool gSubmergedPlantLogged{};

// A UV failure must return the cell to LiquidProxy ownership. Restore every
// typed Tessellator stream and the small amount of public builder state that
// the rejected call advanced, without touching raw offsets or private ABI.
struct TessellatorSuffixCheckpoint {
    std::size_t positions{};
    std::size_t normals{};
    std::size_t tangents{};
    std::size_t indices{};
    std::size_t colors{};
    std::size_t boneIds{};
    std::array<std::size_t, 3> textureUvs{};
    std::size_t pbrTextureIndices{};
    std::size_t mers{};
    std::size_t geoType{};
    std::size_t quadInfo{};
    std::uint32_t count{};
    int curQuadVertex{};
    bool hasNormals{};
    bool indexPhase{};
    bool noColor{};
    unsigned char quadFacing{};
    bool quadTwoSided{};
    Vec3 faceCenterAccumulator{};
    std::pair<glm::vec3, glm::vec3> aabb{};
    std::pair<glm::vec2, glm::vec2> uvAabb{};
    std::array<bool, 15> fieldEnabled{};

    explicit TessellatorSuffixCheckpoint(Tessellator& tessellator) {
        auto const& data = tessellator.mMeshData.get();
        positions = data.mPositions.get().size();
        normals = data.mNormals.get().size();
        tangents = data.mTangents.get().size();
        indices = data.mIndices.get().size();
        colors = data.mColors.get().size();
        boneIds = data.mBoneId0s.get().size();
        for (std::size_t uv = 0; uv < textureUvs.size(); ++uv) {
            textureUvs[uv] = data.mTextureUVs[uv].get().size();
        }
        pbrTextureIndices = data.mPBRTextureIndices.get().size();
        mers = data.mMERS.get().size();
        geoType = data.mGeoType.get().size();
        quadInfo = tessellator.mQuadInfoList.get().size();
        count = tessellator.mCount;
        curQuadVertex = tessellator.mCurQuadVertex;
        hasNormals = tessellator.mHasNormals;
        indexPhase = tessellator.mIndexPhase;
        noColor = tessellator.mNoColor;
        quadFacing = tessellator.mQuadFacing;
        quadTwoSided = tessellator.mQuadTwoSided;
        faceCenterAccumulator = tessellator.mFaceCenterAccumulator;
        aabb = data.mAABB.get();
        uvAabb = data.mUVAABB.get();
        fieldEnabled = data.mFieldEnabled.get();
    }

    void restore(Tessellator& tessellator) const {
        auto& data = tessellator.mMeshData.get();
        data.mPositions.get().resize(positions);
        data.mNormals.get().resize(normals);
        data.mTangents.get().resize(tangents);
        data.mIndices.get().resize(indices);
        data.mColors.get().resize(colors);
        data.mBoneId0s.get().resize(boneIds);
        for (std::size_t uv = 0; uv < textureUvs.size(); ++uv) {
            data.mTextureUVs[uv].get().resize(textureUvs[uv]);
        }
        data.mPBRTextureIndices.get().resize(pbrTextureIndices);
        data.mMERS.get().resize(mers);
        data.mGeoType.get().resize(geoType);
        tessellator.mQuadInfoList.get().resize(quadInfo);
        tessellator.mCount = count;
        tessellator.mCurQuadVertex = curQuadVertex;
        tessellator.mHasNormals = hasNormals;
        tessellator.mIndexPhase = indexPhase;
        tessellator.mNoColor = noColor;
        tessellator.mQuadFacing = quadFacing;
        tessellator.mQuadTwoSided = quadTwoSided;
        tessellator.mFaceCenterAccumulator = faceCenterAccumulator;
        data.mAABB.get() = aabb;
        data.mUVAABB.get() = uvAabb;
        data.mFieldEnabled.get() = fieldEnabled;
    }
};

// Litematica's default schematic overlay palette, converted from ARGB to the
// ABGR byte order of the tessellator vertex color buffer.
constexpr std::uint32_t MissingColorAbgrRgb    = 0x00E6B333U; // #33B3E6
constexpr std::uint32_t ExtraColorAbgrRgb      = 0x00E64CFFU; // #FF4CE6
constexpr std::uint32_t WrongBlockColorAbgrRgb = 0x003333FFU; // #FF3333
constexpr std::uint32_t WrongStateColorAbgrRgb = 0x001090FFU; // #FF9010

constexpr std::uint32_t LiquidWaterTintAbgrRgb = 0x00E4763FU; // #3F76E4
constexpr std::uint32_t LiquidLavaTintAbgrRgb  = 0x00FFFFFFU; // white

void setColorAbgr(Tessellator& tessellator, std::uint32_t colorAbgr) {
    tessellator.color(
        static_cast<float>((colorAbgr >> 0) & 0xFFU) / 255.0f,
        static_cast<float>((colorAbgr >> 8) & 0xFFU) / 255.0f,
        static_cast<float>((colorAbgr >> 16) & 0xFFU) / 255.0f,
        static_cast<float>((colorAbgr >> 24) & 0xFFU) / 255.0f
    );
}

std::uint32_t toAbgr(mce::Color const& color) {
    auto const toByte = [](float component) {
        return static_cast<std::uint32_t>(
            std::lround(255.0f * std::clamp(component, 0.0f, 1.0f))
        );
    };
    return (toByte(color.a) << 24U)
        | (toByte(color.b) << 16U)
        | (toByte(color.g) << 8U)
        | toByte(color.r);
}

std::uint32_t withAlpha(std::uint32_t colorAbgrRgb, float opacity) {
    auto const alpha = static_cast<std::uint32_t>(
        std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f)
    );
    return colorAbgrRgb | (alpha << 24U);
}

std::uint32_t modulateAbgr(std::uint32_t color, std::uint32_t tint) {
    auto const channel = [&](unsigned int shift) {
        auto const value = ((color >> shift) & 0xFFU) * ((tint >> shift) & 0xFFU);
        return ((value + 127U) / 255U) << shift;
    };
    return (color & 0xFF000000U) | channel(0) | channel(8) | channel(16);
}

int correctionPriority(CorrectionState state) {
    return state == CorrectionState::WrongType ? 4
        : state == CorrectionState::WrongState ? 3
        : state == CorrectionState::Missing ? 1
        : 0;
}

template <class Value>
void compactPerVertexField(
    std::vector<Value>&               field,
    std::span<std::uint8_t const>     removeQuads
) {
    if (field.empty()) return;
    std::size_t write{};
    for (std::size_t quad = 0; quad < removeQuads.size(); ++quad) {
        if (removeQuads[quad] != 0U) continue;
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            auto const read = quad * 4U + corner;
            if (write != read) field[write] = std::move(field[read]);
            ++write;
        }
    }
    field.resize(write);
}

template <class Value>
void compactPerQuadField(
    std::vector<Value>&               field,
    std::span<std::uint8_t const>     removeQuads
) {
    if (field.empty()) return;
    std::size_t write{};
    for (std::size_t read = 0; read < removeQuads.size(); ++read) {
        if (removeQuads[read] != 0U) continue;
        if (write != read) field[write] = std::move(field[read]);
        ++write;
    }
    field.resize(write);
}

struct NativeLiquidCullOutcome {
    bool        processed{};
    char const* skipReason{"unknown"};
    std::size_t before{};
    std::size_t culled{};
    std::size_t after{};
    std::size_t pairs{};
    std::size_t indices{};
    std::size_t quadInfo{};
};

NativeLiquidCullOutcome cullNativeLiquidInternalFaces(
    Tessellator&                              tessellator,
    std::vector<PraxisCompatLiquidKind>*      liquidKinds = nullptr
) {
    NativeLiquidCullOutcome outcome{};
    auto& data = tessellator.mMeshData.get();
    auto& positions = data.mPositions.get();
    auto& quadInfo = tessellator.mQuadInfoList.get();
    outcome.before = positions.size();
    outcome.after = positions.size();
    outcome.indices = data.mIndices.get().size();
    outcome.quadInfo = quadInfo.size();

    if (!data.mIndices.get().empty()) {
        outcome.skipReason = "indices_nonempty";
        return outcome;
    }
    if (positions.size() < 8U || positions.size() % 4U != 0U) {
        outcome.skipReason = "malformed_positions";
        return outcome;
    }

    std::array<std::size_t, 10> const fieldCounts{
        data.mNormals.get().size(),
        data.mTangents.get().size(),
        data.mColors.get().size(),
        data.mBoneId0s.get().size(),
        data.mTextureUVs[0].get().size(),
        data.mTextureUVs[1].get().size(),
        data.mTextureUVs[2].get().size(),
        data.mPBRTextureIndices.get().size(),
        data.mMERS.get().size(),
        data.mGeoType.get().size()
    };
    if (!nativeLiquidPerVertexFieldCountsMatch(positions.size(), fieldCounts)) {
        outcome.skipReason = "vertex_field_count_mismatch";
        return outcome;
    }
    if (liquidKinds && liquidKinds->size() != positions.size()) {
        outcome.skipReason = "liquid_kind_count_mismatch";
        return outcome;
    }

    auto const quadCount = positions.size() / 4U;
    if (!quadInfo.empty() && quadInfo.size() != quadCount) {
        outcome.skipReason = "quad_info_count_mismatch";
        return outcome;
    }

    auto const mask = buildNativeLiquidInternalFaceCullMask(
        std::span<glm::vec3 const>{positions.data(), positions.size()}
    );
    if (!mask.valid) {
        outcome.skipReason = "geometry_contract_invalid";
        return outcome;
    }
    outcome.culled = mask.removedVertices();
    outcome.pairs = mask.facePairs;
    if (outcome.culled >= outcome.before && outcome.culled != 0U) {
        // Do not turn an otherwise successful section into an empty retained
        // mesh. A real liquid body should always retain exposed geometry.
        outcome.culled = 0U;
        outcome.pairs = 0U;
        outcome.skipReason = "all_vertices_would_be_removed";
        return outcome;
    }

    if (outcome.culled != 0U) {
        compactPerVertexField(positions, mask.removeQuads);
        compactPerVertexField(data.mNormals.get(), mask.removeQuads);
        compactPerVertexField(data.mTangents.get(), mask.removeQuads);
        compactPerVertexField(data.mColors.get(), mask.removeQuads);
        compactPerVertexField(data.mBoneId0s.get(), mask.removeQuads);
        for (std::size_t uv = 0; uv < 3U; ++uv) {
            compactPerVertexField(data.mTextureUVs[uv].get(), mask.removeQuads);
        }
        compactPerVertexField(data.mPBRTextureIndices.get(), mask.removeQuads);
        compactPerVertexField(data.mMERS.get(), mask.removeQuads);
        compactPerVertexField(data.mGeoType.get(), mask.removeQuads);
        if (liquidKinds) {
            compactPerVertexField(*liquidKinds, mask.removeQuads);
        }
        compactPerQuadField(quadInfo, mask.removeQuads);

        auto minimum = positions.front();
        auto maximum = positions.front();
        for (auto const& position : positions) {
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        data.mAABB.get() = {minimum, maximum};
        auto const& uv0 = data.mTextureUVs[0].get();
        if (!uv0.empty()) {
            auto uvMinimum = uv0.front();
            auto uvMaximum = uv0.front();
            for (auto const& uv : uv0) {
                uvMinimum = glm::min(uvMinimum, uv);
                uvMaximum = glm::max(uvMaximum, uv);
            }
            data.mUVAABB.get() = {uvMinimum, uvMaximum};
        }
        // mCount is a typed/public Fake Headers field consumed by end(). No
        // terminal-state bytes or private renderer offsets are modified.
        tessellator.mCount = static_cast<unsigned int>(positions.size());
    }

    outcome.processed = true;
    outcome.skipReason = "none";
    outcome.after = positions.size();
    return outcome;
}

} // namespace

void buildCorrectionSectionMeshes(
    ProjectionState&,
    Tessellator&,
    std::size_t,
    Tessellator::UploadMode,
    ProjectionSectionBuildSettings const&
);

void buildLiquidProxySectionMesh(
    ProjectionState&,
    Tessellator&,
    std::size_t,
    Tessellator::UploadMode,
    ProjectionSectionBuildSettings const&,
    std::span<std::size_t const>
);

std::vector<std::size_t> buildNativeLiquidSectionMesh(
    ProjectionState&,
    Tessellator&,
    BlockTessellator&,
    BlockSource&,
    std::size_t,
    Tessellator::UploadMode,
    ProjectionSectionBuildSettings const&
);

std::vector<std::size_t> buildPraxisCompatLiquidSectionData(
    ProjectionState&,
    Tessellator&,
    BlockTessellator&,
    std::size_t,
    ProjectionSectionBuildSettings const&
);

void buildBlockEntityPlaceholderSectionMesh(
    ProjectionState&,
    Tessellator&,
    std::size_t,
    Tessellator::UploadMode,
    ProjectionSectionBuildSettings const&,
    std::span<std::size_t const>
);

void buildStructureBoundsMesh(
    ProjectionState&,
    Tessellator&,
    Tessellator::UploadMode,
    ProjectionSectionBuildSettings const&
);

void buildProjectionSection(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockTessellator&                     blockTessellator,
    BlockSource&                          region,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& sectionBuildSettings
) {
    auto const mirror                   = sectionBuildSettings.mirror;
    auto const rotation                 = sectionBuildSettings.rotation;
    auto const mirrorMode               = sectionBuildSettings.mirrorMode;
    auto const rotationTurns            = sectionBuildSettings.rotationTurns;
    auto const offsetX                  = sectionBuildSettings.offsetX;
    auto const offsetY                  = sectionBuildSettings.offsetY;
    auto const offsetZ                  = sectionBuildSettings.offsetZ;
    auto const structureOpacity         = sectionBuildSettings.structureOpacity;
    auto const identityTransform        = sectionBuildSettings.identityTransform;
    LegacyStructureSettings sectionTransformSettings{
        mirror,
        rotation,
        nullptr,
        BoundingBox{}
    };
    constexpr std::size_t NoCompositeBodyOutcome = static_cast<std::size_t>(-1);
    struct LayeredBlock {
        Block const*                  block{};
        BlockPos                      position{};
        BlockRenderLayer              layer{BlockRenderLayer::RenderlayerOpaque};
        RenderBucket bucket{RenderBucket::Opaque};
        std::size_t                   structureIndex{};
        std::size_t compositeOutcomeIndex{NoCompositeBodyOutcome};
    };
    struct CompositeBodyOutcome {
        std::size_t  structureIndex{};
        Block const* body{};
        Block const* liquid{};
        std::uint64_t vertices{};
    };
    // Blocks whose model produced no geometry during tessellation
    // (block-entity blocks such as chests and signs) get a placeholder.
    std::vector<std::size_t> failedTessellationIndices;
    std::vector<LayeredBlock> layeredBlocks;
    std::vector<CompositeBodyOutcome> compositeBodyOutcomes;
    layeredBlocks.reserve(state.sectionBlockIndices[section].size() * 2);
    for (auto const index : state.sectionBlockIndices[section]) {
        auto const correctionState = state.correctionStates[index];
        // Never draw a projected block model on top of an existing
        // world block. Correct blocks disappear; wrong type/state use
        // only their red/yellow outline below. This removes the
        // coincident textured surfaces that caused correction flicker.
        if (correctionState == CorrectionState::Correct
            || correctionState == CorrectionState::WrongType
            || correctionState == CorrectionState::WrongState) {
            continue;
        }
        auto const& entry = state.structure->renderBlocks[index];
        auto const transformed = transformStructurePosition(entry, *state.structure, mirrorMode, rotationTurns);
        BlockPos const position{
            state.anchor.x + offsetX + transformed.x,
            state.anchor.y + offsetY + transformed.y,
            state.anchor.z + offsetZ + transformed.z
        };
        std::size_t compositeOutcomeIndex = NoCompositeBodyOutcome;
        if (entry.block && entry.liquid) {
            compositeOutcomeIndex = compositeBodyOutcomes.size();
            compositeBodyOutcomes.push_back({
                index,
                entry.block,
                entry.liquid,
                0U
            });
        }
        auto const appendBlock = [&](Block const* source) {
            auto const* transformedBlock = transformExpectedBlock(source, sectionTransformSettings, identityTransform);
            if (!transformedBlock) return;
            auto const& typeName = transformedBlock->getTypeName();
            if (typeName == VanillaBlockTypeIds::PistonArmCollision().getString()
                || typeName == VanillaBlockTypeIds::StickyPistonArmCollision().getString()) {
                return;
            }
            auto* graphics = BlockGraphics::getForBlock(*transformedBlock);
            auto const primaryLayer = graphics
                ? graphics->getRenderLayer(region, position)
                : (transformedBlock->getBlockType().mIsOpaqueFullBlock
                    ? BlockRenderLayer::RenderlayerOpaque
                    : BlockRenderLayer::RenderlayerAlphatest);
            std::uint32_t appendedLayerMask{};
            auto const appendLayer = [&](BlockRenderLayer layer) {
                auto const value = static_cast<unsigned int>(layer);
                if (value >= 32 || (appendedLayerMask & (1U << value)) != 0) return;
                appendedLayerMask |= 1U << value;
                layeredBlocks.push_back({
                    transformedBlock,
                    position,
                    layer,
                    renderBucketFor(layer),
                    index,
                    compositeOutcomeIndex
                });
            };
            appendLayer(primaryLayer);
            if (graphics) {
                // This is Minecraft's current BlockGraphics classification,
                // not an LHolo family guess. A block may request more than
                // one of the 22 native RenderChunk layers.
                auto const extraMask = static_cast<std::uint32_t>(
                    // Fake Headers exposes this native virtual as non-const
                    // even though getForBlock returns the shared graphics
                    // object as const. The query is the engine's classifier;
                    // no LHolo-owned state is written here.
                    const_cast<BlockGraphics*>(graphics)->getExtraRenderLayers()
                );
                auto const layerCount = static_cast<unsigned int>(
                    BlockRenderLayer::RenderlayerCount
                );
                for (unsigned int value = 0; value < layerCount; ++value) {
                    if ((extraMask & (1U << value)) == 0) continue;
                    appendLayer(static_cast<BlockRenderLayer>(value));
                }
            }
        };
        appendBlock(entry.block);
    }
    BlockPos const origin{
        state.anchor.x + offsetX,
        state.anchor.y + offsetY,
        state.anchor.z + offsetZ
    };
    constexpr std::array<char const*, static_cast<std::size_t>(RenderBucket::Count)>
        meshNames{
            "LHoloOpaque",
            "LHoloAlpha",
            "LHoloAlphaOneSided",
            "LHoloBlend"
        };
    ScopedTessellationBlocks tessellationBlocksScope(
        *state.expectedWorldBlocks,
        *state.expectedWorldLiquids,
        *state.expectedWorldBlockActors,
        &state.nativeLiquidTelemetry
    );
    for (std::size_t bucketIndex = 0;
         bucketIndex < static_cast<std::size_t>(RenderBucket::Count);
         ++bucketIndex) {
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList,
            std::max(128, static_cast<int>(layeredBlocks.size() * 24)),
            false
        );
        bool bucketTessellated{};
        auto const bucket = static_cast<RenderBucket>(bucketIndex);
        for (auto const& layered : layeredBlocks) {
            if (layered.bucket != bucket) continue;
            blockTessellator.mRenderingLayer = static_cast<int>(layered.layer);
            auto const tintMethod = layered.block->getBlockType().mTintMethod;
            std::optional<std::uint32_t> foliageTint;
            if (tintMethod == TintMethod::DefaultFoliage
                || tintMethod == TintMethod::BirchFoliage
                || tintMethod == TintMethod::EvergreenFoliage
                || tintMethod == TintMethod::DryFoliage) {
                // BlockTessellator::buildBiomeWeights() is not exported by
                // 1.26.40 and the tessellator's biome weight cache is never
                // populated, so pass null and let the policy sample the biome
                // color fresh from the region for this position.
                foliageTint = toAbgr(BiomeColorSampling::getTessellationPolicy(tintMethod).get(
                    *layered.block,
                    region,
                    layered.position,
                    nullptr
                ));
            }
            // Flattened connection blocks (fences, glass panes, iron bars,
            // ...) draw their arms from derived states the structure palette
            // never stores, so a raw palette permutation tessellates as a
            // bare post no matter which neighbors are visible. Recompute the
            // connections with the vanilla update against the virtual
            // neighborhood; inside this scope the hooked BlockSource::getBlock
            // answers with the projected blocks, and the update's own region
            // write is suppressed so the real world stays untouched.
            Block const& renderBlock = withFlattenedConnections(
                *layered.block, region, layered.position
            );
            auto const firstPosition = tessellator.mMeshData->mPositions.get().size();
            auto const firstColor = tessellator.mMeshData->mColors.get().size();
            auto const rendered = blockTessellator.tessellateInWorld(
                tessellator, renderBlock, layered.position, true
            );
            // Several legacy shape tessellators (notably doors) return
            // false after successfully appending vertices. The return
            // value describes the dispatch path, not mesh production.
            // Trust the actual mesh delta so those models are retained.
            auto const geometryAdded =
                tessellator.mMeshData->mPositions.get().size() > firstPosition;
            auto const verticesAdded =
                tessellator.mMeshData->mPositions.get().size() - firstPosition;
            if (layered.compositeOutcomeIndex != NoCompositeBodyOutcome) {
                auto& composite =
                    compositeBodyOutcomes[layered.compositeOutcomeIndex];
                composite.vertices += verticesAdded;
                auto const logIndex = gSubmergedBodyLogCount.fetch_add(
                    1U,
                    std::memory_order_acq_rel
                );
                if (logIndex < 8U) {
                    logger().info(
                        "PRAXIS_SUBMERGED_BODY body={} liquid={} layer={} returned={} verticesAdded={}",
                        layered.block->getTypeName(),
                        composite.liquid->getTypeName(),
                        static_cast<unsigned int>(layered.layer),
                        rendered ? 1 : 0,
                        verticesAdded
                    );
                }
            }
            if (!rendered && !geometryAdded) {
                // No terrain-atlas model: needs a placeholder hull.
                failedTessellationIndices.push_back(layered.structureIndex);
                continue;
            }
            auto& colors = tessellator.mMeshData->mColors.get();
            if (foliageTint) {
                for (std::size_t colorIndex = firstColor; colorIndex < colors.size(); ++colorIndex) {
                    colors[colorIndex] = modulateAbgr(colors[colorIndex], *foliageTint);
                }
            }
            for (std::size_t colorIndex = firstColor; colorIndex < colors.size(); ++colorIndex) {
                colors[colorIndex] = applyGhostAppearanceAbgr(
                    colors[colorIndex], structureOpacity
                );
            }
            bucketTessellated = true;
        }
        auto& destination = state.sections[section].meshes[bucketIndex];
        if (!bucketTessellated) {
            tessellator.end(
                Tessellator::UploadMode::Never,
                meshNames[bucketIndex],
                SupplementaryFieldAutoGenerationMode{0}
            );
            destination.reset();
            continue;
        }
        for (auto& vertex : tessellator.mMeshData->mPositions.get()) {
            vertex.x -= static_cast<float>(origin.x);
            vertex.y -= static_cast<float>(origin.y);
            vertex.z -= static_cast<float>(origin.z);
        }
        destination = std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            meshNames[bucketIndex],
            SupplementaryFieldAutoGenerationMode{1}
        ));
    }

    state.nativeLiquidTelemetry.compositeBodyLiquidCells
        += compositeBodyOutcomes.size();
    std::uint64_t sectionCompositePositive{};
    std::uint64_t sectionCompositeZero{};
    std::uint64_t sectionCompositeVertices{};
    for (auto const& outcome : compositeBodyOutcomes) {
        state.nativeLiquidTelemetry.compositeBodyVertices += outcome.vertices;
        sectionCompositeVertices += outcome.vertices;
        if (outcome.vertices != 0U) {
            ++state.nativeLiquidTelemetry.compositeBodyTessellationPositive;
            ++sectionCompositePositive;
        } else {
            ++state.nativeLiquidTelemetry.compositeBodyTessellationZero;
            ++sectionCompositeZero;
        }
        auto const& bodyName = outcome.body->getTypeName();
        if ((bodyName.find("kelp") != std::string::npos
             || bodyName.find("seagrass") != std::string::npos)
            && !gSubmergedPlantLogged.exchange(true, std::memory_order_acq_rel)) {
            logger().info(
                "PRAXIS_SUBMERGED_PLANT body={} liquid={} verticesAdded={}",
                bodyName,
                outcome.liquid->getTypeName(),
                outcome.vertices
            );
        }
    }
    if (!compositeBodyOutcomes.empty()) {
        logger().info(
            "PRAXIS_SUBMERGED_BODY_TELEMETRY compositeCells={} positive={} zero={} vertices={} section={}",
            compositeBodyOutcomes.size(),
            sectionCompositePositive,
            sectionCompositeZero,
            sectionCompositeVertices,
            section
        );
    }

    std::sort(failedTessellationIndices.begin(), failedTessellationIndices.end());
    failedTessellationIndices.erase(
        std::unique(failedTessellationIndices.begin(), failedTessellationIndices.end()),
        failedTessellationIndices.end()
    );

    std::vector<std::size_t> nativeLiquidSucceeded;
    std::vector<std::size_t> praxisCompatLiquidSucceeded;
    if (ActiveNativeLiquidRenderPath == NativeLiquidRenderPath::PraxisCompat) {
        auto const compatAttemptsBefore =
            state.nativeLiquidTelemetry.praxisCompatCellsAttempted;
        praxisCompatLiquidSucceeded = detail::buildPraxisCompatLiquidSectionData(
            state,
            tessellator,
            blockTessellator,
            section,
            sectionBuildSettings
        );
        if (state.praxisCompatLiquidSections[section]) {
            // The exact replay stream owns this section. Do not spend a second
            // BlockTessellator pass building the retained comparison mesh.
            state.nativeLiquidSectionMeshes[section].reset();
            state.nativeLiquidSectionCellCounts[section] = 0U;
        } else if (state.nativeLiquidTelemetry.praxisCompatCellsAttempted
                   != compatAttemptsBefore) {
            // Build the old route only as a section-local fail-closed fallback.
            // Successful PraxisExactReplay sections never enter this branch.
            ++state.nativeLiquidTelemetry.praxisCompatDoubleLiquidBuildSections;
            nativeLiquidSucceeded = detail::buildNativeLiquidSectionMesh(
                state,
                tessellator,
                blockTessellator,
                region,
                section,
                uploadMode,
                sectionBuildSettings
            );
        }
    } else {
        state.praxisCompatLiquidSections[section].reset();
        nativeLiquidSucceeded = detail::buildNativeLiquidSectionMesh(
            state,
            tessellator,
            blockTessellator,
            region,
            section,
            uploadMode,
            sectionBuildSettings
        );
    }
    auto const& liquidOwnership = state.praxisCompatLiquidSections[section]
        ? praxisCompatLiquidSucceeded
        : nativeLiquidSucceeded;
    detail::buildLiquidProxySectionMesh(
        state,
        tessellator,
        section,
        uploadMode,
        sectionBuildSettings,
        liquidOwnership
    );
    detail::buildBlockEntityPlaceholderSectionMesh(
        state,
        tessellator,
        section,
        uploadMode,
        sectionBuildSettings,
        failedTessellationIndices
    );
    detail::buildCorrectionSectionMeshes(
        state, tessellator, section, uploadMode, sectionBuildSettings
    );
    detail::buildStructureBoundsMesh(
        state, tessellator, uploadMode, sectionBuildSettings
    );
}

std::vector<std::size_t> buildNativeLiquidSectionMesh(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockTessellator&                     blockTessellator,
    BlockSource&                          region,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings
) {
    LegacyStructureSettings sectionTransformSettings{
        settings.mirror,
        settings.rotation,
        nullptr,
        BoundingBox{}
    };
    std::vector<std::size_t> candidates;
    candidates.reserve(state.sectionLiquidBlockIndices[section].size());
    for (auto const index : state.sectionLiquidBlockIndices[section]) {
        if (state.correctionStates[index] != CorrectionState::Missing) continue;
        candidates.push_back(index);
    }

    std::vector<std::size_t> succeeded;
    state.nativeLiquidSectionCellCounts[section] = 0;
    if (candidates.empty()) {
        state.nativeLiquidSectionMeshes[section].reset();
        return succeeded;
    }

    tessellator.begin(
        Tessellator::DebugContextCallback{},
        mce::PrimitiveMode::QuadList,
        std::max(128, static_cast<int>(candidates.size() * 24)),
        false
    );
    auto const origin = BlockPos{
        state.anchor.x + settings.offsetX,
        state.anchor.y + settings.offsetY,
        state.anchor.z + settings.offsetZ
    };
    for (auto const index : candidates) {
        ++state.nativeLiquidTelemetry.nativeLiquidCellsAttempted;
        auto const& entry = state.structure->renderBlocks[index];
        auto const* expectedLiquid = transformExpectedBlock(
            entry.liquid,
            sectionTransformSettings,
            settings.identityTransform
        );
        if (!expectedLiquid) {
            ++state.nativeLiquidTelemetry.nativeLiquidTessellationFailure;
            continue;
        }
        auto const local = transformStructurePosition(
            entry,
            *state.structure,
            settings.mirrorMode,
            settings.rotationTurns
        );
        BlockPos const worldPosition{
            origin.x + local.x,
            origin.y + local.y,
            origin.z + local.z
        };

        std::array<BlockRenderLayer, 22> layers{};
        std::size_t layerCount{};
        std::uint32_t layerMask{};
        auto const appendLayer = [&](BlockRenderLayer layer) {
            auto const value = static_cast<unsigned int>(layer);
            if (value >= layers.size() || (layerMask & (1U << value)) != 0) return;
            layerMask |= 1U << value;
            layers[layerCount++] = layer;
        };
        auto const* graphics = BlockGraphics::getForBlock(*expectedLiquid);
        appendLayer(graphics
            ? graphics->getRenderLayer(region, worldPosition)
            : BlockRenderLayer::RenderlayerBlend);
        if (graphics) {
            auto const extraMask = static_cast<std::uint32_t>(
                const_cast<BlockGraphics*>(graphics)->getExtraRenderLayers()
            );
            for (unsigned int value = 0; value < layers.size(); ++value) {
                if ((extraMask & (1U << value)) != 0) {
                    appendLayer(static_cast<BlockRenderLayer>(value));
                }
            }
        }
        NativeLiquidAtlasRect atlasRect{};
        if (graphics) {
            auto const& texture = graphics->getTexture(0, 0);
            atlasRect = {texture._u0, texture._v0, texture._u1, texture._v1};
        }
        if (!graphics || !isValidNativeLiquidAtlasRect(atlasRect)) {
            ++state.nativeLiquidTelemetry.nativeLiquidUvRemapFailures;
            if (!gNativeLiquidUvFailureLogged.exchange(true, std::memory_order_acq_rel)) {
                logger().warn(
                    "NATIVE_LIQUID_UV_REMAP_FAILURE type={} reason=invalid_atlas_rect atlas=({}, {}, {}, {})",
                    expectedLiquid->getTypeName(),
                    atlasRect.u0,
                    atlasRect.v0,
                    atlasRect.u1,
                    atlasRect.v1
                );
            }
            continue;
        }
        ++state.nativeLiquidTelemetry.nativeLiquidUvAtlasResolvedCells;
        logger().debug(
            "NATIVE_LIQUID_RENDER_LAYERS type={} pos=({}, {}, {}) primary={} mask=0x{:X}",
            expectedLiquid->getTypeName(),
            worldPosition.x,
            worldPosition.y,
            worldPosition.z,
            static_cast<unsigned int>(layers[0]),
            layerMask
        );

        TessellatorSuffixCheckpoint const cellCheckpoint{tessellator};
        bool cellPositive{};
        bool cellFailure{};
        bool cellUvFailure{};
        std::uint64_t cellVertices{};
        std::uint64_t cellUvVertices{};
        std::uint64_t cellColorVertices{};
        std::uint64_t cellAlphaModifiedVertices{};
        bool lastReturned{};
        std::size_t lastPositionsBefore{};
        std::size_t lastPositionsAfter{};
        std::size_t lastColorsBefore{};
        std::size_t lastColorsAfter{};
        std::size_t lastUvsBefore{};
        std::size_t lastUvsAfter{};
        auto const virtualHitsBefore = state.nativeLiquidTelemetry.virtualLiquidQueryHits;
        unsigned int lastLayer{};
        for (std::size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
            auto const layer = layers[layerIndex];
            auto const layerValue = static_cast<unsigned int>(layer);
            ++state.nativeLiquidTelemetry.nativeLiquidLayerAttempts[layerValue];
            blockTessellator.mRenderingLayer = static_cast<int>(layer);
            auto& positions = tessellator.mMeshData->mPositions.get();
            auto& colors = tessellator.mMeshData->mColors.get();
            auto& uvs = tessellator.mMeshData->mTextureUVs[0].get();
            auto const positionsBefore = positions.size();
            auto const colorsBefore = colors.size();
            auto const uvsBefore = uvs.size();
            bool rendered{};
            try {
                rendered = blockTessellator.tessellateInWorld(
                    tessellator,
                    *expectedLiquid,
                    worldPosition,
                    true
                );
            } catch (std::exception const& exception) {
                if (positions.size() != positionsBefore || colors.size() != colorsBefore
                    || uvs.size() != uvsBefore) throw;
                cellFailure = true;
                logger().warn(
                    "NATIVE_LIQUID_TESSELLATION_FAILURE type={} layer={} pos=({}, {}, {}) exception={}",
                    expectedLiquid->getTypeName(),
                    layerValue,
                    worldPosition.x,
                    worldPosition.y,
                    worldPosition.z,
                    exception.what()
                );
                break;
            } catch (...) {
                if (positions.size() != positionsBefore || colors.size() != colorsBefore
                    || uvs.size() != uvsBefore) throw;
                cellFailure = true;
                logger().warn(
                    "NATIVE_LIQUID_TESSELLATION_FAILURE type={} layer={} pos=({}, {}, {}) exception=unknown",
                    expectedLiquid->getTypeName(),
                    layerValue,
                    worldPosition.x,
                    worldPosition.y,
                    worldPosition.z
                );
                break;
            }
            auto const positionsAfter = positions.size();
            auto const colorsAfter = colors.size();
            auto const uvsAfter = uvs.size();
            auto const geometryAdded = positionsAfter > positionsBefore;
            lastReturned = rendered;
            lastPositionsBefore = positionsBefore;
            lastPositionsAfter = positionsAfter;
            lastColorsBefore = colorsBefore;
            lastColorsAfter = colorsAfter;
            lastUvsBefore = uvsBefore;
            lastUvsAfter = uvsAfter;
            lastLayer = layerValue;
            if (!geometryAdded) continue;

            auto const added = positionsAfter - positionsBefore;
            auto const addedUvs = uvsAfter > uvsBefore ? uvsAfter - uvsBefore : 0U;
            NativeLiquidUvRemapDiagnostics uvDiagnostics{};
            auto const uvRemapped = addedUvs == added
                && remapNativeLiquidUvToAtlas(
                    std::span<glm::vec2>{uvs.data() + uvsBefore, addedUvs},
                    atlasRect,
                    &uvDiagnostics
                );
            if (!uvRemapped) {
                cellCheckpoint.restore(tessellator);
                cellPositive = false;
                cellUvFailure = true;
                if (!gNativeLiquidUvFailureLogged.exchange(true, std::memory_order_acq_rel)) {
                    logger().warn(
                        "NATIVE_LIQUID_UV_REMAP_FAILURE type={} reason=invalid_uv_suffix positions={} uv0={}",
                        expectedLiquid->getTypeName(),
                        added,
                        addedUvs
                    );
                }
                break;
            }
            if (!gNativeLiquidUvRemapLogged.exchange(true, std::memory_order_acq_rel)) {
                logger().info(
                    "NATIVE_LIQUID_UV_REMAP type={} atlas=({}, {}, {}, {}) vertices={} rawUvMinMax=({}, {}, {}, {}) mappedAtlasRect=({}, {}, {}, {})",
                    expectedLiquid->getTypeName(),
                    atlasRect.u0,
                    atlasRect.v0,
                    atlasRect.u1,
                    atlasRect.v1,
                    uvDiagnostics.remappedVertices,
                    uvDiagnostics.firstQuadMinU,
                    uvDiagnostics.firstQuadMaxU,
                    uvDiagnostics.firstQuadMinV,
                    uvDiagnostics.firstQuadMaxV,
                    atlasRect.u0,
                    atlasRect.v0,
                    atlasRect.u1,
                    atlasRect.v1
                );
            }
            for (std::size_t colorIndex = colorsBefore; colorIndex < colorsAfter; ++colorIndex) {
                auto const before = colors[colorIndex];
                auto const after = applyGhostAppearanceAbgr(
                    colors[colorIndex],
                    settings.structureOpacity
                );
                colors[colorIndex] = after;
                cellAlphaModifiedVertices += (before >> 24U) != (after >> 24U);
            }
            cellVertices += added;
            cellUvVertices += addedUvs;
            cellColorVertices += colorsAfter > colorsBefore ? colorsAfter - colorsBefore : 0;
            cellPositive = true;
            if (!gNativeLiquidPositiveLogged.exchange(true, std::memory_order_acq_rel)) {
                logger().info(
                    "NATIVE_LIQUID_TESSELLATION_POSITIVE type={} layer={} pos=({}, {}, {}) returned={} positions={}->{} colors={}->{} uv0={}->{} vertices={} virtualLiquidHitsDelta={}",
                    expectedLiquid->getTypeName(),
                    layerValue,
                    worldPosition.x,
                    worldPosition.y,
                    worldPosition.z,
                    rendered,
                    positionsBefore,
                    positionsAfter,
                    colorsBefore,
                    colorsAfter,
                    uvsBefore,
                    uvsAfter,
                    added,
                    state.nativeLiquidTelemetry.virtualLiquidQueryHits - virtualHitsBefore
                );
            }
        }
        if (cellUvFailure) {
            ++state.nativeLiquidTelemetry.nativeLiquidUvRemapFailures;
        } else if (cellPositive) {
            ++state.nativeLiquidTelemetry.nativeLiquidTessellationPositive;
            state.nativeLiquidTelemetry.nativeLiquidVertices += cellVertices;
            state.nativeLiquidTelemetry.nativeLiquidUvVertices += cellUvVertices;
            state.nativeLiquidTelemetry.nativeLiquidUvRemappedVertices += cellUvVertices;
            state.nativeLiquidTelemetry.nativeLiquidColorVertices += cellColorVertices;
            state.nativeLiquidTelemetry.nativeLiquidAlphaModifiedVertices
                += cellAlphaModifiedVertices;
            succeeded.push_back(index);
        } else if (cellFailure) {
            ++state.nativeLiquidTelemetry.nativeLiquidTessellationFailure;
        } else {
            ++state.nativeLiquidTelemetry.nativeLiquidTessellationZero;
            logger().debug(
                "NATIVE_LIQUID_TESSELLATION_ZERO type={} layer={} pos=({}, {}, {}) returned={} positions={}->{} colors={}->{} uv0={}->{} virtualLiquidHitsDelta={}",
                expectedLiquid->getTypeName(),
                lastLayer,
                worldPosition.x,
                worldPosition.y,
                worldPosition.z,
                lastReturned,
                lastPositionsBefore,
                lastPositionsAfter,
                lastColorsBefore,
                lastColorsAfter,
                lastUvsBefore,
                lastUvsAfter,
                state.nativeLiquidTelemetry.virtualLiquidQueryHits - virtualHitsBefore
            );
        }
    }

    if (succeeded.empty()) {
        tessellator.end(
            Tessellator::UploadMode::Never,
            "LHoloNativeLiquid",
            SupplementaryFieldAutoGenerationMode{0}
        );
        state.nativeLiquidSectionMeshes[section].reset();
        return succeeded;
    }
    auto const cull = cullNativeLiquidInternalFaces(tessellator);
    state.nativeLiquidTelemetry.nativeLiquidVerticesBeforeCull += cull.before;
    state.nativeLiquidTelemetry.nativeLiquidVerticesAfterCull += cull.after;
    if (cull.processed) {
        state.nativeLiquidTelemetry.nativeLiquidVerticesCulled += cull.culled;
        state.nativeLiquidTelemetry.nativeLiquidFacePairsCulled += cull.pairs;
        if (!gNativeLiquidCullLogged.exchange(true, std::memory_order_acq_rel)) {
            logger().info(
                "NATIVE_LIQUID_INTERNAL_FACE_CULL before={} culled={} after={} pairs={} indices={} quadInfo={}",
                cull.before,
                cull.culled,
                cull.after,
                cull.pairs,
                cull.indices,
                cull.quadInfo
            );
        }
    } else {
        ++state.nativeLiquidTelemetry.nativeLiquidCullSkipped;
        if (!gNativeLiquidCullSkipLogged.exchange(true, std::memory_order_acq_rel)) {
            logger().warn(
                "NATIVE_LIQUID_INTERNAL_FACE_CULL_SKIPPED reason={} vertices={} indices={} quadInfo={}",
                cull.skipReason,
                cull.before,
                cull.indices,
                cull.quadInfo
            );
        }
    }
    for (auto& vertex : tessellator.mMeshData->mPositions.get()) {
        vertex.x -= static_cast<float>(origin.x);
        vertex.y -= static_cast<float>(origin.y);
        vertex.z -= static_cast<float>(origin.z);
    }
    state.nativeLiquidSectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
        uploadMode,
        "LHoloNativeLiquid",
        SupplementaryFieldAutoGenerationMode{1}
    ));
    std::sort(succeeded.begin(), succeeded.end());
    state.nativeLiquidSectionCellCounts[section] = succeeded.size();
    return succeeded;
}

std::vector<std::size_t> buildPraxisCompatLiquidSectionData(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockTessellator&                     blockTessellator,
    std::size_t                           section,
    ProjectionSectionBuildSettings const& settings
) {
    static_assert(
        static_cast<unsigned int>(BlockRenderLayer::RenderlayerBlend) == 3U,
        "Praxis compatibility liquid layer must remain native layer 3"
    );
    state.praxisCompatLiquidSections[section].reset();

    LegacyStructureSettings sectionTransformSettings{
        settings.mirror,
        settings.rotation,
        nullptr,
        BoundingBox{}
    };
    std::vector<std::size_t> candidates;
    std::vector<PraxisCompatLiquidKind> liquidKinds;
    candidates.reserve(state.sectionLiquidBlockIndices[section].size());
    for (auto const index : state.sectionLiquidBlockIndices[section]) {
        if (state.correctionStates[index] != CorrectionState::Missing) continue;
        candidates.push_back(index);
    }
    std::vector<std::size_t> succeeded;
    if (candidates.empty()) return succeeded;

    // This is the separate Praxis 1.21.132 compatibility candidate. The
    // existing LHolo retained build above remains false/true; this build uses
    // the proven true/false generation contract without sharing its output.
    tessellator.begin(
        Tessellator::DebugContextCallback{},
        mce::PrimitiveMode::QuadList,
        std::max(128, static_cast<int>(candidates.size() * 24)),
        true
    );
    auto const origin = BlockPos{
        state.anchor.x + settings.offsetX,
        state.anchor.y + settings.offsetY,
        state.anchor.z + settings.offsetZ
    };
    constexpr auto PraxisLiquidLayer = BlockRenderLayer::RenderlayerBlend;
    blockTessellator.mRenderingLayer = static_cast<int>(PraxisLiquidLayer);

    for (auto const index : candidates) {
        ++state.nativeLiquidTelemetry.praxisCompatCellsAttempted;
        auto const& entry = state.structure->renderBlocks[index];
        auto const* expectedLiquid = transformExpectedBlock(
            entry.liquid,
            sectionTransformSettings,
            settings.identityTransform
        );
        if (!expectedLiquid) {
            ++state.nativeLiquidTelemetry.praxisCompatTessellationFailure;
            continue;
        }
        auto const local = transformStructurePosition(
            entry,
            *state.structure,
            settings.mirrorMode,
            settings.rotationTurns
        );
        BlockPos const worldPosition{
            origin.x + local.x,
            origin.y + local.y,
            origin.z + local.z
        };

        auto const* graphics = BlockGraphics::getForBlock(*expectedLiquid);
        NativeLiquidAtlasRect atlasRect{};
        if (graphics) {
            auto const& texture = graphics->getTexture(0, 0);
            atlasRect = {texture._u0, texture._v0, texture._u1, texture._v1};
        }
        if (!graphics || !isValidNativeLiquidAtlasRect(atlasRect)) {
            ++state.nativeLiquidTelemetry.praxisCompatUvRemapFailures;
            continue;
        }

        TessellatorSuffixCheckpoint const checkpoint{tessellator};
        auto& positions = tessellator.mMeshData->mPositions.get();
        auto& colors = tessellator.mMeshData->mColors.get();
        auto& uvs = tessellator.mMeshData->mTextureUVs[0].get();
        auto const positionsBefore = positions.size();
        auto const colorsBefore = colors.size();
        auto const uvsBefore = uvs.size();
        bool rendered{};
        try {
            rendered = blockTessellator.tessellateInWorld(
                tessellator,
                *expectedLiquid,
                worldPosition,
                false
            );
        } catch (std::exception const& exception) {
            if (positions.size() != positionsBefore || colors.size() != colorsBefore
                || uvs.size() != uvsBefore) {
                checkpoint.restore(tessellator);
            }
            ++state.nativeLiquidTelemetry.praxisCompatTessellationFailure;
            logger().warn(
                "PRAXIS_COMPAT_LIQUID_TESSELLATION_FAILURE type={} layer=3 pos=({}, {}, {}) exception={}",
                expectedLiquid->getTypeName(),
                worldPosition.x,
                worldPosition.y,
                worldPosition.z,
                exception.what()
            );
            continue;
        } catch (...) {
            if (positions.size() != positionsBefore || colors.size() != colorsBefore
                || uvs.size() != uvsBefore) {
                checkpoint.restore(tessellator);
            }
            ++state.nativeLiquidTelemetry.praxisCompatTessellationFailure;
            logger().warn(
                "PRAXIS_COMPAT_LIQUID_TESSELLATION_FAILURE type={} layer=3 pos=({}, {}, {}) exception=unknown",
                expectedLiquid->getTypeName(),
                worldPosition.x,
                worldPosition.y,
                worldPosition.z
            );
            continue;
        }

        auto const positionsAfter = positions.size();
        auto const uvsAfter = uvs.size();
        if (positionsAfter <= positionsBefore) {
            ++state.nativeLiquidTelemetry.praxisCompatTessellationZero;
            logger().debug(
                "PRAXIS_COMPAT_LIQUID_TESSELLATION_ZERO type={} layer=3 pos=({}, {}, {}) returned={}",
                expectedLiquid->getTypeName(),
                worldPosition.x,
                worldPosition.y,
                worldPosition.z,
                rendered
            );
            continue;
        }

        auto const addedVertices = positionsAfter - positionsBefore;
        auto const addedUvs = uvsAfter > uvsBefore ? uvsAfter - uvsBefore : 0U;
        auto const uvRemapped = addedUvs == addedVertices
            && remapNativeLiquidUvToAtlas(
                std::span<glm::vec2>{uvs.data() + uvsBefore, addedUvs},
                atlasRect
            );
        if (!uvRemapped) {
            checkpoint.restore(tessellator);
            ++state.nativeLiquidTelemetry.praxisCompatUvRemapFailures;
            continue;
        }

        ++state.nativeLiquidTelemetry.praxisCompatTessellationPositive;
        state.nativeLiquidTelemetry.praxisCompatVertices += addedVertices;
        state.nativeLiquidTelemetry.praxisCompatUvRemappedVertices += addedUvs;
        auto const liquidKind = expectedLiquid->getBlockType().mMaterial.mSuperHot
            ? PraxisCompatLiquidKind::Lava
            : PraxisCompatLiquidKind::Water;
        liquidKinds.insert(liquidKinds.end(), addedVertices, liquidKind);
        succeeded.push_back(index);
    }

    // Compatibility ownership is section-atomic. Any missing cell keeps this
    // section on the unchanged retained/proxy fallback instead of mixing two
    // visual contracts in one translucent body.
    if (succeeded.size() != candidates.size()) {
        tessellator.end(
            Tessellator::UploadMode::Never,
            "LHoloPraxisCompatLiquidRejected",
            SupplementaryFieldAutoGenerationMode{0}
        );
        succeeded.clear();
        return succeeded;
    }

    auto const cull = cullNativeLiquidInternalFaces(tessellator, &liquidKinds);
    state.nativeLiquidTelemetry.praxisCompatVerticesBeforeCull += cull.before;
    state.nativeLiquidTelemetry.praxisCompatVerticesAfterCull += cull.after;
    if (!cull.processed) {
        ++state.nativeLiquidTelemetry.praxisCompatCullSkipped;
        tessellator.end(
            Tessellator::UploadMode::Never,
            "LHoloPraxisCompatLiquidCullRejected",
            SupplementaryFieldAutoGenerationMode{0}
        );
        succeeded.clear();
        return succeeded;
    }
    state.nativeLiquidTelemetry.praxisCompatVerticesCulled += cull.culled;
    state.nativeLiquidTelemetry.praxisCompatFacePairsCulled += cull.pairs;

    auto& meshData = tessellator.mMeshData.get();
    auto& positions = meshData.mPositions.get();
    auto& colors = meshData.mColors.get();
    auto& uvs = meshData.mTextureUVs[0].get();
    if (positions.empty() || positions.size() != colors.size()
        || positions.size() != uvs.size()
        || positions.size() != liquidKinds.size()) {
        ++state.nativeLiquidTelemetry.praxisCompatTessellationFailure;
        tessellator.end(
            Tessellator::UploadMode::Never,
            "LHoloPraxisCompatLiquidStreamRejected",
            SupplementaryFieldAutoGenerationMode{0}
        );
        succeeded.clear();
        return succeeded;
    }

    // Convert the complete native stream into projection-origin local space.
    // Supplementary vectors remain byte-for-byte/native-value copies.
    for (auto& vertex : positions) {
        vertex.x -= static_cast<float>(origin.x);
        vertex.y -= static_cast<float>(origin.y);
        vertex.z -= static_cast<float>(origin.z);
    }
    for (auto& quad : tessellator.mQuadInfoList.get()) {
        auto& centroid = quad.centroid.get();
        centroid.x -= static_cast<float>(origin.x);
        centroid.y -= static_cast<float>(origin.y);
        centroid.z -= static_cast<float>(origin.z);
    }
    auto minimum = positions.front();
    auto maximum = positions.front();
    for (auto const& position : positions) {
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    meshData.mAABB.get() = {minimum, maximum};

    auto compat = std::make_unique<PraxisCompatLiquidSectionData>();
    compat->derivedColors.reserve(colors.size());
    compat->liquidKinds = liquidKinds;
    std::uint64_t waterSeedVertices{};
    std::uint64_t lavaNativeVertices{};
    for (std::size_t vertex = 0; vertex < colors.size(); ++vertex) {
        auto const source = colors[vertex];
        auto const isWater = liquidKinds[vertex] == PraxisCompatLiquidKind::Water;
        auto const seed = selectPraxisCompatLiquidColorSeed(source, isWater);
        auto const derived = applyPraxisCompatLiquidAlpha(
            applyPraxisCompatMissingAbgr(seed.packed),
            isWater
        );
        compat->derivedColors.push_back(derived);
        if (seed.waterSeedApplied) {
            ++waterSeedVertices;
            if (!gPraxisLiquidColorSeedLogged.exchange(
                    true,
                    std::memory_order_acq_rel
                )) {
                auto const nativeRgba = unpackAbgr(source);
                auto const seedRgba = unpackAbgr(seed.packed);
                auto const derivedRgba = unpackAbgr(derived);
                logger().info(
                    "PRAXIS_LIQUID_COLOR_SEED type=minecraft:water nativeSource=({},{},{},{}) compatSeed=({},{},{},{}) derived=({},{},{},{})",
                    nativeRgba.red,
                    nativeRgba.green,
                    nativeRgba.blue,
                    nativeRgba.alpha,
                    seedRgba.red,
                    seedRgba.green,
                    seedRgba.blue,
                    seedRgba.alpha,
                    derivedRgba.red,
                    derivedRgba.green,
                    derivedRgba.blue,
                    derivedRgba.alpha
                );
            }
        } else if (!isWater) {
            ++lavaNativeVertices;
        }
    }
    state.nativeLiquidTelemetry.praxisCompatWaterSeedVertices
        += waterSeedVertices;
    state.nativeLiquidTelemetry.praxisCompatLavaNativeVertices
        += lavaNativeVertices;
    compat->nativeStream = std::make_unique<mce::MeshData>(meshData);
    compat->tessellatorState = {
        .isFormatFixed        = tessellator.mIsFormatFixed,
        .hasNormals           = tessellator.mHasNormals,
        .indexPhase           = tessellator.mIndexPhase,
        .noColor              = tessellator.mNoColor,
        .buildFaceData        = tessellator.mBuildFaceData,
        .quadFacing           = tessellator.mQuadFacing,
        .quadTwoSided         = tessellator.mQuadTwoSided,
        .curQuadVertex        = tessellator.mCurQuadVertex,
        .count                = tessellator.mCount,
        .maxVertexCount       = tessellator.mMaxVertexCount,
        .faceCenterAccumulator = tessellator.mFaceCenterAccumulator,
        .quadInfo             = tessellator.mQuadInfoList.get()
    };
    if (!compat->ready()) {
        ++state.nativeLiquidTelemetry.praxisCompatTessellationFailure;
        tessellator.end(
            Tessellator::UploadMode::Never,
            "LHoloPraxisCompatLiquidPayloadRejected",
            SupplementaryFieldAutoGenerationMode{0}
        );
        succeeded.clear();
        return succeeded;
    }

    if (!gPraxisCompatLiquidColorLogged.exchange(true, std::memory_order_acq_rel)) {
        auto const& sourceColors = compat->nativeStream->mColors.get();
        auto const sampleCount = std::min<std::size_t>(4U, sourceColors.size());
        for (std::size_t sample = 0; sample < sampleCount; ++sample) {
            auto const source = unpackAbgr(sourceColors[sample]);
            auto const derived = unpackAbgr(compat->derivedColors[sample]);
            logger().info(
                "PRAXIS_COMPAT_LIQUID_COLOR vertex={} sourceRGBA=({}, {}, {}, {}) derivedRGBA=({}, {}, {}, {})",
                sample,
                source.red,
                source.green,
                source.blue,
                source.alpha,
                derived.red,
                derived.green,
                derived.blue,
                derived.alpha
            );
        }
    }

    state.nativeLiquidTelemetry.praxisCompatDerivedColorVertices
        += compat->derivedColors.size();
    state.nativeLiquidTelemetry.praxisCompatCapturedPositions += positions.size();
    state.nativeLiquidTelemetry.praxisCompatCapturedNormals
        += meshData.mNormals.get().size();
    state.nativeLiquidTelemetry.praxisCompatCapturedTangents
        += meshData.mTangents.get().size();
    state.nativeLiquidTelemetry.praxisCompatCapturedColors += colors.size();
    state.nativeLiquidTelemetry.praxisCompatCapturedBoneIds
        += meshData.mBoneId0s.get().size();
    state.nativeLiquidTelemetry.praxisCompatCapturedUv0 += uvs.size();
    state.nativeLiquidTelemetry.praxisCompatCapturedUv1
        += meshData.mTextureUVs[1].get().size();
    state.nativeLiquidTelemetry.praxisCompatCapturedUv2
        += meshData.mTextureUVs[2].get().size();
    state.nativeLiquidTelemetry.praxisCompatCapturedPbrTextureIndices
        += meshData.mPBRTextureIndices.get().size();
    state.nativeLiquidTelemetry.praxisCompatCapturedMers
        += meshData.mMERS.get().size();
    state.nativeLiquidTelemetry.praxisCompatCapturedGeoType
        += meshData.mGeoType.get().size();
    state.nativeLiquidTelemetry.praxisCompatCapturedQuadInfo
        += compat->tessellatorState.quadInfo.size();
    ++state.nativeLiquidTelemetry.praxisCompatBuildSections;
    std::sort(succeeded.begin(), succeeded.end());
    tessellator.end(
        Tessellator::UploadMode::Never,
        "LHoloPraxisCompatLiquidBuild",
        SupplementaryFieldAutoGenerationMode{0}
    );
    state.praxisCompatLiquidSections[section] = std::move(compat);
    return succeeded;
}

void buildLiquidProxySectionMesh(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings,
    std::span<std::size_t const>           nativeLiquidSucceeded
) {
    auto const mirrorMode        = settings.mirrorMode;
    auto const rotationTurns     = settings.rotationTurns;
    auto const offsetX           = settings.offsetX;
    auto const offsetY           = settings.offsetY;
    auto const offsetZ           = settings.offsetZ;
    auto const structureOpacity  = settings.structureOpacity;
    auto const identityTransform = settings.identityTransform;
    LegacyStructureSettings sectionTransformSettings{
        settings.mirror,
        settings.rotation,
        nullptr,
        BoundingBox{}
    };
    // Textured liquid proxy hulls. LHolo never lies to the vanilla
    // world or chunk pipeline (that leaks into gameplay), so missing
    // liquids draw as translucent hulls here. The hulls reuse the
    // vanilla terrain-atlas water/lava tiles and travel the exact
    // material path used for glass, keeping them purely cosmetic.
    std::vector<std::size_t> liquidProxyIndices;
    for (auto const index : state.sectionBlockIndices[section]) {
        if (state.structure->renderBlocks[index].liquid == nullptr) continue;
        if (state.correctionStates[index] != CorrectionState::Missing) continue;
        if (std::binary_search(
                nativeLiquidSucceeded.begin(),
                nativeLiquidSucceeded.end(),
                index
            )) {
            continue;
        }
        liquidProxyIndices.push_back(index);
    }
    state.liquidProxySectionCellCounts[section] = liquidProxyIndices.size();
    state.nativeLiquidTelemetry.liquidProxyFallbackCells += liquidProxyIndices.size();
    if (!liquidProxyIndices.empty()) {
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList,
            static_cast<int>(liquidProxyIndices.size() * 24),
            false
        );
        auto const alpha = static_cast<uint>(std::lround(
            std::clamp(structureOpacity, 0.05f, 1.0f) * 255.0f
        ));
        for (auto const index : liquidProxyIndices) {
            auto const& entry = state.structure->renderBlocks[index];
            auto const* expectedLiquid = transformExpectedBlock(entry.liquid, sectionTransformSettings, identityTransform);
            if (!expectedLiquid) continue;
            auto const* graphics = BlockGraphics::getForBlock(*expectedLiquid);
            auto const* uvSet = graphics ? &graphics->getTexture(0, 0) : nullptr;
            auto const p = transformStructurePosition(entry, *state.structure, mirrorMode, rotationTurns);
            BlockPos const worldPosition{
                state.anchor.x + offsetX + p.x,
                state.anchor.y + offsetY + p.y,
                state.anchor.z + offsetZ + p.z
            };
            auto const neighborEntry = [&](int dx, int dy, int dz)
                -> structure::LoadedStructure::RenderBlock const* {
                auto const found = state.expectedWorldBlockIndices->find(std::tuple{
                    worldPosition.x + dx, worldPosition.y + dy, worldPosition.z + dz
                });
                return found == state.expectedWorldBlockIndices->end()
                    ? nullptr : &state.structure->renderBlocks[found->second];
            };
            auto const neighborIsSameLiquid = [&](int dx, int dy, int dz) {
                auto const* neighbor = neighborEntry(dx, dy, dz);
                if (!neighbor || !neighbor->liquid) return false;
                auto const* transformed = transformExpectedBlock(neighbor->liquid, sectionTransformSettings, identityTransform);
                return transformed && transformed->getTypeName() == expectedLiquid->getTypeName();
            };
            // Flow-aware surface. Source and submerged cells stay full;
            // flowing cells taper by liquid_depth, and each top corner is
            // averaged from the surrounding same-liquid columns (an air
            // column pulls a corner down toward the spill). The result is
            // a surface that slopes downhill, showing the flow direction.
            constexpr float surface = 8.0f / 9.0f;
            auto const liquidDepth = [](Block const& block) -> int {
                for (auto const& [key, value] : block.mSerializationId.get()) {
                    if (key != "states" || !value.hold<::CompoundTag>()) continue;
                    for (auto const& [stateKey, stateValue] : value.get<::CompoundTag>()) {
                        if (stateKey == "liquid_depth" && stateValue.getId() == ::Tag::Type::Int)
                            return stateValue.get<::IntTag>().data;
                    }
                }
                return 0;
            };
            auto const fluidHeight = [](int depth) -> float {
                if (depth <= 0) return 8.0f / 9.0f;   // source
                if (depth >= 8) return 1.0f;          // falling counts as full
                return (8.0f - static_cast<float>(depth)) / 9.0f;
            };
            // Height (0..1) of the same-liquid column at (dx,dz); -1 for a
            // solid/other block (ignored), 0 for air (spill).
            auto const columnHeight = [&](int dx, int dz) -> float {
                auto const* n = (dx == 0 && dz == 0) ? &entry : neighborEntry(dx, 0, dz);
                if (!n) return 0.0f;
                if (!n->liquid) return -1.0f;
                auto const* t = transformExpectedBlock(n->liquid, sectionTransformSettings, identityTransform);
                if (!t || t->getTypeName() != expectedLiquid->getTypeName()) return -1.0f;
                if (neighborIsSameLiquid(dx, 1, dz)) return 1.0f;  // submerged
                return fluidHeight(liquidDepth(*t));
            };
            auto const cornerHeight = [&](int dx, int dz) -> float {
                float best = -1.0f, sum = 0.0f;
                int   count = 0;
                int const offsets[4][2] = {{0, 0}, {dx, 0}, {0, dz}, {dx, dz}};
                for (auto const& o : offsets) {
                    float const h = columnHeight(o[0], o[1]);
                    if (h < 0.0f) continue;  // solid: does not affect the surface
                    best = std::max(best, h);
                    sum += h;
                    ++count;
                }
                if (best >= surface) return best;  // a source/full column keeps it high
                return count > 0 ? sum / static_cast<float>(count) : surface;
            };
            auto const tint = expectedLiquid->getBlockType().mMaterial.mSuperHot
                ? (LiquidLavaTintAbgrRgb | (alpha << 24U))
                : (LiquidWaterTintAbgrRgb | (alpha << 24U));
            float const x0 = static_cast<float>(p.x);
            float const y0 = static_cast<float>(p.y);
            float const z0 = static_cast<float>(p.z);
            float const x1 = static_cast<float>(p.x + 1);
            float const z1 = static_cast<float>(p.z + 1);
            // Per-corner top heights (world Y). c<x><z>: x0/x1, z0/z1.
            float const yc00 = y0 + cornerHeight(-1, -1);
            float const yc10 = y0 + cornerHeight( 1, -1);
            float const yc01 = y0 + cornerHeight(-1,  1);
            float const yc11 = y0 + cornerHeight( 1,  1);
            // Full-tile UVs when the atlas tile is available; a tiny
            // degenerate UV otherwise still renders as flat tint.
            float const u0 = uvSet ? uvSet->_u0 : 0.0f;
            float const v0 = uvSet ? uvSet->_v0 : 0.0f;
            float const u1 = uvSet ? uvSet->_u1 : 0.0f;
            float const v1 = uvSet ? uvSet->_v1 : 0.0f;
            auto addLiquidFace = [&](
                Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d
            ) {
                setColorAbgr(tessellator, tint);
                tessellator.tex2({u0, v0}); tessellator.vertex(a.x, a.y, a.z);
                tessellator.tex2({u0, v1}); tessellator.vertex(b.x, b.y, b.z);
                tessellator.tex2({u1, v1}); tessellator.vertex(c.x, c.y, c.z);
                tessellator.tex2({u1, v0}); tessellator.vertex(d.x, d.y, d.z);
            };
            if (!neighborEntry(0, -1, 0))
                addLiquidFace({x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1});
            if (!neighborIsSameLiquid(0, 1, 0))
                addLiquidFace({x0,yc00,z0}, {x0,yc01,z1}, {x1,yc11,z1}, {x1,yc10,z0});
            if (!neighborIsSameLiquid(0, 0, -1))
                addLiquidFace({x0,y0,z0}, {x0,yc00,z0}, {x1,yc10,z0}, {x1,y0,z0});
            if (!neighborIsSameLiquid(0, 0, 1))
                addLiquidFace({x1,y0,z1}, {x1,yc11,z1}, {x0,yc01,z1}, {x0,y0,z1});
            if (!neighborIsSameLiquid(-1, 0, 0))
                addLiquidFace({x0,y0,z1}, {x0,yc01,z1}, {x0,yc00,z0}, {x0,y0,z0});
            if (!neighborIsSameLiquid(1, 0, 0))
                addLiquidFace({x1,y0,z0}, {x1,yc10,z0}, {x1,yc11,z1}, {x1,y0,z1});
        }
        state.liquidProxySectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloLiquidProxy",
            SupplementaryFieldAutoGenerationMode{0}
        ));
    } else {
        state.liquidProxySectionMeshes[section].reset();
    }

}

void buildBlockEntityPlaceholderSectionMesh(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings,
    std::span<std::size_t const>           failedTessellationIndices
) {
    auto const mirrorMode        = settings.mirrorMode;
    auto const rotationTurns     = settings.rotationTurns;
    auto const structureOpacity  = settings.structureOpacity;
    auto const identityTransform = settings.identityTransform;
    LegacyStructureSettings sectionTransformSettings{
        settings.mirror,
        settings.rotation,
        nullptr,
        BoundingBox{}
    };
    // Blocks that tessellated to nothing (block-entity blocks such as
    // chests and signs) get a textured placeholder hull from their
    // BlockGraphics tile so the projection still shows them. Blocks
    // that render normally (hoppers, beds, ...) are left untouched.
    std::vector<std::size_t> blockEntityIndices;
    for (auto const index : failedTessellationIndices) {
        if (state.correctionStates[index] != CorrectionState::Missing) continue;
        if (state.blockActorRendererAvailable[index]) continue;
        blockEntityIndices.push_back(index);
    }
    if (!blockEntityIndices.empty()) {
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList,
            static_cast<int>(blockEntityIndices.size() * 24),
            false
        );
        auto const alpha = static_cast<uint>(std::lround(
            std::clamp(structureOpacity, 0.05f, 1.0f) * 255.0f
        ));
        auto const tint = 0x00FFFFFFU | (alpha << 24U);
        for (auto const index : blockEntityIndices) {
            auto const& entry = state.structure->renderBlocks[index];
            auto const* expectedBlock = transformExpectedBlock(entry.block, sectionTransformSettings, identityTransform);
            if (!expectedBlock) continue;
            auto const* graphics = BlockGraphics::getForBlock(*expectedBlock);
            auto const* uvSet = graphics ? &graphics->getTexture(0, 0) : nullptr;
            auto const p = transformStructurePosition(entry, *state.structure, mirrorMode, rotationTurns);
            // Signs are thin planks, not full cubes.
            bool const isSign = expectedBlock->getTypeName().find("sign") != std::string::npos;
            // Facing (FacingDirection state: North=2 South=3 West=4 East=5)
            // decides which cube face is the block's front.
            int const frontFace = blockFrontFace(*expectedBlock);
            float const px = static_cast<float>(p.x);
            float const py = static_cast<float>(p.y);
            float const pz = static_cast<float>(p.z);
            float x0 = px, y0 = py, z0 = pz;
            float x1 = px + 1.0f, y1 = py + 1.0f, z1 = pz + 1.0f;
            if (isSign) {
                // Thin plank, 0.125 thick along the facing axis.
                float const mid = (frontFace == static_cast<int>(Facing::Name::West)
                    || frontFace == static_cast<int>(Facing::Name::East))
                    ? px + 0.5f : pz + 0.5f;
                if (frontFace == static_cast<int>(Facing::Name::West)
                    || frontFace == static_cast<int>(Facing::Name::East)) {
                    x0 = mid - 0.0625f;
                    x1 = mid + 0.0625f;
                } else {
                    z0 = mid - 0.0625f;
                    z1 = mid + 0.0625f;
                }
            }
            float const u0 = uvSet ? uvSet->_u0 : 0.0f;
            float const v0 = uvSet ? uvSet->_v0 : 0.0f;
            float const u1 = uvSet ? uvSet->_u1 : 0.0f;
            float const v1 = uvSet ? uvSet->_v1 : 0.0f;
            auto const frontColor = tint;
            auto const dimColor = (0x00888888U & 0x00FFFFFFU) | (alpha << 24U);
            auto addFace = [&](
                Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d, bool isFront
            ) {
                auto const color = isFront ? frontColor : dimColor;
                setColorAbgr(tessellator, color);
                tessellator.tex2({u0, v0}); tessellator.vertex(a.x, a.y, a.z);
                tessellator.tex2({u0, v1}); tessellator.vertex(b.x, b.y, b.z);
                tessellator.tex2({u1, v1}); tessellator.vertex(c.x, c.y, c.z);
                tessellator.tex2({u1, v0}); tessellator.vertex(d.x, d.y, d.z);
            };
            bool const northFront = frontFace == static_cast<int>(Facing::Name::North);
            bool const southFront = frontFace == static_cast<int>(Facing::Name::South);
            bool const westFront = frontFace == static_cast<int>(Facing::Name::West);
            bool const eastFront = frontFace == static_cast<int>(Facing::Name::East);
            addFace({x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, false); // bottom
            addFace({x0,y1,z0}, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, false); // top
            addFace({x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {x1,y0,z0}, northFront); // north
            addFace({x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, {x0,y0,z1}, southFront); // south
            addFace({x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {x0,y0,z0}, westFront); // west
            addFace({x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {x1,y0,z1}, eastFront); // east
        }
        state.blockEntityPlaceholderSectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloBlockEntityPlaceholder",
            SupplementaryFieldAutoGenerationMode{0}
        ));
    } else {
        state.blockEntityPlaceholderSectionMeshes[section].reset();
    }

}

void buildCorrectionSectionMeshes(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings
) {
    // Split the correction geometry into "missing" and "wrong" (WrongType +
    // WrongState) so the see-through option can X-ray only the wrong markers,
    // never the many "missing" outlines.
    auto const isWrongState = [](CorrectionState correction) {
        return correction == CorrectionState::WrongType
            || correction == CorrectionState::WrongState;
    };
    std::size_t missingCount{};
    std::size_t wrongCount{};
    for (auto const index : state.sectionBlockIndices[section]) {
        auto const correction = state.correctionStates[index];
        if (isWrongState(correction)) ++wrongCount;
        else if (correction == CorrectionState::Missing) ++missingCount;
    }
    wrongCount += state.sectionExtraBlockPositions[section].size();
    if (missingCount == 0 && wrongCount == 0) {
        state.warningFillSectionMeshes[section].reset();
        state.correctionOutlineSectionMeshes[section].reset();
        state.wrongFillSectionMeshes[section].reset();
        state.wrongOutlineSectionMeshes[section].reset();
        return;
    }

    constexpr float outlineInset  = 0.0f;
    constexpr float outlineExtent = 1.0f;
    // Every overlay vertex aims at the center of the vanilla 2x2 pure-white
    // texture so the overlay materials' texture lookups stay a neutral opaque
    // (1,1,1,1) instead of sampling the missing-texture checkerboard.
    auto addOutlineEdge = [&](Vec3 const& first, Vec3 const& second) {
        tessellator.tex2({0.5f, 0.5f}); tessellator.vertex(first.x, first.y, first.z);
        tessellator.tex2({0.5f, 0.5f}); tessellator.vertex(second.x, second.y, second.z);
    };
    // Use true LineList geometry rendered with the vanilla outline material.
    auto buildOutline = [&](bool wantWrong, std::size_t count) -> std::unique_ptr<mce::Mesh> {
        if (count == 0) return nullptr;
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::LineList,
            static_cast<int>(count * 24),
            false
        );
        for (auto const index : state.sectionBlockIndices[section]) {
            auto const correction = state.correctionStates[index];
            auto const priority = correctionPriority(correction);
            if (priority == 0) continue;
            if (isWrongState(correction) != wantWrong) continue;
            auto const& entry = state.structure->renderBlocks[index];
            // A missing pure-liquid cell is already communicated by its blue
            // translucent proxy hull; skip the duplicate outline.
            if (correction == CorrectionState::Missing && !entry.block && entry.liquid) continue;
            auto const p = transformStructurePosition(
                entry, *state.structure, settings.mirrorMode, settings.rotationTurns
            );
            auto const outlineColor = correction == CorrectionState::Missing
                ? withAlpha(MissingColorAbgrRgb, settings.correctionOutlineOpacity)
                : correction == CorrectionState::WrongState
                    ? withAlpha(WrongStateColorAbgrRgb, settings.correctionOutlineOpacity)
                    : withAlpha(WrongBlockColorAbgrRgb, settings.correctionOutlineOpacity);
            float const x0 = static_cast<float>(p.x) + outlineInset;
            float const y0 = static_cast<float>(p.y) + outlineInset;
            float const z0 = static_cast<float>(p.z) + outlineInset;
            float const x1 = static_cast<float>(p.x) + outlineExtent;
            float const y1 = static_cast<float>(p.y) + outlineExtent;
            float const z1 = static_cast<float>(p.z) + outlineExtent;
            setColorAbgr(tessellator, outlineColor);
            addOutlineEdge({x0,y0,z0},{x1,y0,z0}); addOutlineEdge({x1,y0,z0},{x1,y1,z0});
            addOutlineEdge({x1,y1,z0},{x0,y1,z0}); addOutlineEdge({x0,y1,z0},{x0,y0,z0});
            addOutlineEdge({x0,y0,z1},{x1,y0,z1}); addOutlineEdge({x1,y0,z1},{x1,y1,z1});
            addOutlineEdge({x1,y1,z1},{x0,y1,z1}); addOutlineEdge({x0,y1,z1},{x0,y0,z1});
            addOutlineEdge({x0,y0,z0},{x0,y0,z1}); addOutlineEdge({x1,y0,z0},{x1,y0,z1});
            addOutlineEdge({x1,y1,z0},{x1,y1,z1}); addOutlineEdge({x0,y1,z0},{x0,y1,z1});
        }
        if (wantWrong) {
            setColorAbgr(tessellator, withAlpha(
                ExtraColorAbgrRgb, settings.correctionOutlineOpacity
            ));
            for (auto const& [x, y, z] : state.sectionExtraBlockPositions[section]) {
                auto const p = transformStructurePosition(
                    BlockPos{x, y, z}, *state.structure,
                    settings.mirrorMode, settings.rotationTurns
                );
                float const x0 = static_cast<float>(p.x) + outlineInset;
                float const y0 = static_cast<float>(p.y) + outlineInset;
                float const z0 = static_cast<float>(p.z) + outlineInset;
                float const x1 = static_cast<float>(p.x) + outlineExtent;
                float const y1 = static_cast<float>(p.y) + outlineExtent;
                float const z1 = static_cast<float>(p.z) + outlineExtent;
                addOutlineEdge({x0,y0,z0},{x1,y0,z0}); addOutlineEdge({x1,y0,z0},{x1,y1,z0});
                addOutlineEdge({x1,y1,z0},{x0,y1,z0}); addOutlineEdge({x0,y1,z0},{x0,y0,z0});
                addOutlineEdge({x0,y0,z1},{x1,y0,z1}); addOutlineEdge({x1,y0,z1},{x1,y1,z1});
                addOutlineEdge({x1,y1,z1},{x0,y1,z1}); addOutlineEdge({x0,y1,z1},{x0,y0,z1});
                addOutlineEdge({x0,y0,z0},{x0,y0,z1}); addOutlineEdge({x1,y0,z0},{x1,y0,z1});
                addOutlineEdge({x1,y1,z0},{x1,y1,z1}); addOutlineEdge({x0,y1,z0},{x0,y1,z1});
            }
        }
        return std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloCorrectionOutline",
            SupplementaryFieldAutoGenerationMode{0}
        ));
    };
    state.correctionOutlineSectionMeshes[section] = buildOutline(false, missingCount);
    state.wrongOutlineSectionMeshes[section] = buildOutline(true, wrongCount);

    // Litematica-style correction fill: an exact untextured 1x1x1 cell overlay.
    // Rasterizer bias supplies depth separation at submission time.
    auto addFillFace = [&](Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d) {
        tessellator.tex2({0.5f, 0.5f}); tessellator.vertex(a.x, a.y, a.z);
        tessellator.tex2({0.5f, 0.5f}); tessellator.vertex(b.x, b.y, b.z);
        tessellator.tex2({0.5f, 0.5f}); tessellator.vertex(c.x, c.y, c.z);
        tessellator.tex2({0.5f, 0.5f}); tessellator.vertex(d.x, d.y, d.z);
    };
    auto buildFill = [&](bool wantWrong, std::size_t count) -> std::unique_ptr<mce::Mesh> {
        if (count == 0) return nullptr;
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList,
            static_cast<int>(count * 24),
            false
        );
        auto const neighborPriority = [&](BlockPos const& p, int dx, int dy, int dz) {
            BlockPos const worldPosition{
                state.anchor.x + settings.offsetX + p.x + dx,
                state.anchor.y + settings.offsetY + p.y + dy,
                state.anchor.z + settings.offsetZ + p.z + dz
            };
            auto const expected = state.expectedWorldBlockIndices->find(std::tuple{
                worldPosition.x, worldPosition.y, worldPosition.z
            });
            auto priority = expected == state.expectedWorldBlockIndices->end()
                ? 0 : correctionPriority(state.correctionStates[expected->second]);
            auto const local = inverseTransformStructurePosition(
                BlockPos{p.x + dx, p.y + dy, p.z + dz},
                *state.structure,
                settings.mirrorMode,
                settings.rotationTurns
            );
            if (state.extraBlockPositions.contains(
                    std::tuple{local.x, local.y, local.z}
                )) {
                priority = std::max(priority, 2);
            }
            return priority;
        };
        for (auto const index : state.sectionBlockIndices[section]) {
            auto const correction = state.correctionStates[index];
            auto const priority = correctionPriority(correction);
            if (priority == 0) continue;
            if (isWrongState(correction) != wantWrong) continue;
            auto const& entry = state.structure->renderBlocks[index];
            if (correction == CorrectionState::Missing && !entry.block && entry.liquid) continue;
            auto const p = transformStructurePosition(
                entry, *state.structure, settings.mirrorMode, settings.rotationTurns
            );
            // Face-culling stays global across categories so a wrong cell next
            // to a missing cell still hides the lower-priority shared face.
            float const x0 = static_cast<float>(p.x);
            float const y0 = static_cast<float>(p.y);
            float const z0 = static_cast<float>(p.z);
            float const x1 = static_cast<float>(p.x + 1);
            float const y1 = static_cast<float>(p.y + 1);
            float const z1 = static_cast<float>(p.z + 1);
            auto const fillColor = correction == CorrectionState::Missing
                ? withAlpha(MissingColorAbgrRgb, settings.correctionFillOpacity)
                : correction == CorrectionState::WrongState
                    ? withAlpha(WrongStateColorAbgrRgb, settings.correctionFillOpacity)
                    : withAlpha(WrongBlockColorAbgrRgb, settings.correctionFillOpacity);
            setColorAbgr(tessellator, fillColor);
            if (priority > neighborPriority(p, 0, 0, -1)) addFillFace({x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {x1,y0,z0});
            if (priority > neighborPriority(p, 0, 0, 1))  addFillFace({x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, {x0,y0,z1});
            if (priority > neighborPriority(p, -1, 0, 0)) addFillFace({x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {x0,y0,z0});
            if (priority > neighborPriority(p, 1, 0, 0))  addFillFace({x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {x1,y0,z1});
            if (priority > neighborPriority(p, 0, -1, 0)) addFillFace({x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1});
            if (priority > neighborPriority(p, 0, 1, 0))  addFillFace({x0,y1,z0}, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0});
        }
        if (wantWrong) {
            constexpr int priority = 2;
            setColorAbgr(tessellator, withAlpha(
                ExtraColorAbgrRgb, settings.correctionFillOpacity
            ));
            for (auto const& [x, y, z] : state.sectionExtraBlockPositions[section]) {
                auto const p = transformStructurePosition(
                    BlockPos{x, y, z}, *state.structure,
                    settings.mirrorMode, settings.rotationTurns
                );
                float const x0 = static_cast<float>(p.x);
                float const y0 = static_cast<float>(p.y);
                float const z0 = static_cast<float>(p.z);
                float const x1 = static_cast<float>(p.x + 1);
                float const y1 = static_cast<float>(p.y + 1);
                float const z1 = static_cast<float>(p.z + 1);
                if (priority > neighborPriority(p, 0, 0, -1)) addFillFace({x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {x1,y0,z0});
                if (priority > neighborPriority(p, 0, 0, 1))  addFillFace({x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, {x0,y0,z1});
                if (priority > neighborPriority(p, -1, 0, 0)) addFillFace({x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {x0,y0,z0});
                if (priority > neighborPriority(p, 1, 0, 0))  addFillFace({x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {x1,y0,z1});
                if (priority > neighborPriority(p, 0, -1, 0)) addFillFace({x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1});
                if (priority > neighborPriority(p, 0, 1, 0))  addFillFace({x0,y1,z0}, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0});
            }
        }
        return std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloWarningFill",
            SupplementaryFieldAutoGenerationMode{0}
        ));
    };
    state.warningFillSectionMeshes[section] = buildFill(false, missingCount);
    state.wrongFillSectionMeshes[section] = buildFill(true, wrongCount);
}

void buildStructureBoundsMesh(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings
) {
    if (uploadMode == Tessellator::UploadMode::Never || state.structureBoundsMesh) return;
    auto const rotated = settings.rotationTurns == 1 || settings.rotationTurns == 3;
    auto const width = static_cast<float>(rotated ? state.structure->sizeZ : state.structure->sizeX);
    auto const height = static_cast<float>(state.structure->sizeY);
    auto const depth = static_cast<float>(rotated ? state.structure->sizeX : state.structure->sizeZ);
    // The wireframe wraps the structure extent exactly, with no outward
    // expansion.
    float const x0 = 0.0f, y0 = 0.0f, z0 = 0.0f;
    float const x1 = width;
    float const y1 = height;
    float const z1 = depth;
    tessellator.begin(
        Tessellator::DebugContextCallback{}, mce::PrimitiveMode::LineList, 24, false
    );
    setColorAbgr(tessellator, 0xFFFFD633U);
    auto addBoundsEdge = [&](Vec3 const& a, Vec3 const& b) {
        // Center of the pure-white texture: the overlay material's alpha test
        // samples it and never discards.
        tessellator.tex2({0.5f, 0.5f}); tessellator.vertex(a.x, a.y, a.z);
        tessellator.tex2({0.5f, 0.5f}); tessellator.vertex(b.x, b.y, b.z);
    };
    addBoundsEdge({x0,y0,z0},{x1,y0,z0}); addBoundsEdge({x1,y0,z0},{x1,y1,z0});
    addBoundsEdge({x1,y1,z0},{x0,y1,z0}); addBoundsEdge({x0,y1,z0},{x0,y0,z0});
    addBoundsEdge({x0,y0,z1},{x1,y0,z1}); addBoundsEdge({x1,y0,z1},{x1,y1,z1});
    addBoundsEdge({x1,y1,z1},{x0,y1,z1}); addBoundsEdge({x0,y1,z1},{x0,y0,z1});
    addBoundsEdge({x0,y0,z0},{x0,y0,z1}); addBoundsEdge({x1,y0,z0},{x1,y0,z1});
    addBoundsEdge({x1,y1,z0},{x1,y1,z1}); addBoundsEdge({x0,y1,z0},{x0,y1,z1});
    state.structureBoundsMesh = std::make_unique<mce::Mesh>(tessellator.end(
        uploadMode,
        "LHoloStructureBounds",
        SupplementaryFieldAutoGenerationMode{0}
    ));
}

} // namespace lholo::projection::detail
