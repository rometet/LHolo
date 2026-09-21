// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Internal value types shared by projection implementation units. The exact
// replay payload owns a CPU-side copy of native MeshData; GPU ownership remains
// with Minecraft's immediate submission path.

#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "mc/client/renderer/TessellatorQuadInfo.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/minecraft_renderer/renderer/MeshData.h"
#include "mc/world/level/BlockPos.h"

class Block;
class BlockActor;

namespace lholo::projection::detail {

using SubChunkKey = std::tuple<int, int, int>;

enum class CorrectionState : std::uint8_t { Unknown, Missing, Correct, WrongType, WrongState };
enum class RenderBucket : std::uint8_t { Opaque, Alpha, AlphaOneSided, Blend, Count };
enum class NativeLiquidRenderPath : std::uint8_t { LHoloRetained, PraxisCompat };

#if defined(LHOLO_NATIVE_LIQUID_RETAINED_DIAGNOSTIC)
inline constexpr NativeLiquidRenderPath ActiveNativeLiquidRenderPath =
    NativeLiquidRenderPath::LHoloRetained;
#else
inline constexpr NativeLiquidRenderPath ActiveNativeLiquidRenderPath =
    NativeLiquidRenderPath::PraxisCompat;
#endif

struct PraxisCompatTessellatorState {
    bool                                  isFormatFixed{};
    bool                                  hasNormals{};
    bool                                  indexPhase{};
    bool                                  noColor{};
    bool                                  buildFaceData{};
    unsigned char                         quadFacing{};
    bool                                  quadTwoSided{};
    int                                   curQuadVertex{};
    std::uint32_t                         count{};
    std::uint32_t                         maxVertexCount{};
    Vec3                                  faceCenterAccumulator{};
    std::vector<TessellatorQuadInfo>       quadInfo;
};

// The native MeshData copy is the canonical stream. In particular, its
// mColors remain the untouched BlockTessellator output. Only derivedColors is
// LHolo-owned, so exact replay can replace packed color without reconstructing
// positions, normals, tangents, UVs or supplementary streams vertex-by-vertex.
struct PraxisCompatLiquidSectionData {
    std::unique_ptr<mce::MeshData> nativeStream;
    std::vector<std::uint32_t> derivedColors;
    PraxisCompatTessellatorState tessellatorState;

    [[nodiscard]] bool ready() const noexcept {
        if (!nativeStream) return false;
        auto const vertexCount = nativeStream->mPositions.get().size();
        auto const fullOrEmpty = [vertexCount](std::size_t count) {
            return count == 0U || count == vertexCount;
        };
        auto const& quadInfo = tessellatorState.quadInfo;
        return vertexCount != 0U
            && vertexCount % 4U == 0U
            && nativeStream->mMode == mce::PrimitiveMode::QuadList
            && nativeStream->mIndices.get().empty()
            && nativeStream->mColors.get().size() == vertexCount
            && nativeStream->mTextureUVs[0].get().size() == vertexCount
            && fullOrEmpty(nativeStream->mNormals.get().size())
            && fullOrEmpty(nativeStream->mTangents.get().size())
            && fullOrEmpty(nativeStream->mBoneId0s.get().size())
            && fullOrEmpty(nativeStream->mTextureUVs[1].get().size())
            && fullOrEmpty(nativeStream->mTextureUVs[2].get().size())
            && fullOrEmpty(nativeStream->mPBRTextureIndices.get().size())
            && fullOrEmpty(nativeStream->mMERS.get().size())
            && fullOrEmpty(nativeStream->mGeoType.get().size())
            && (quadInfo.empty() || quadInfo.size() == vertexCount / 4U)
            && derivedColors.size() == vertexCount;
    }
};

// Native-liquid counters. Build counters accumulate in worker snapshots and
// merge only after a matching revision is accepted; the explicit PerFrame and
// timing fields are refreshed by the active render owner.
struct NativeLiquidTelemetry {
    std::uint64_t nativeLiquidCellsAttempted{};
    std::uint64_t nativeLiquidTessellationPositive{};
    std::uint64_t nativeLiquidTessellationZero{};
    std::uint64_t nativeLiquidTessellationFailure{};
    std::uint64_t nativeLiquidVertices{};
    std::uint64_t nativeLiquidUvVertices{};
    std::uint64_t nativeLiquidUvAtlasResolvedCells{};
    std::uint64_t nativeLiquidUvRemappedVertices{};
    std::uint64_t nativeLiquidUvRemapFailures{};
    std::uint64_t nativeLiquidVerticesBeforeCull{};
    std::uint64_t nativeLiquidVerticesCulled{};
    std::uint64_t nativeLiquidVerticesAfterCull{};
    std::uint64_t nativeLiquidFacePairsCulled{};
    std::uint64_t nativeLiquidCullSkipped{};
    std::uint64_t praxisCompatCellsAttempted{};
    std::uint64_t praxisCompatTessellationPositive{};
    std::uint64_t praxisCompatTessellationZero{};
    std::uint64_t praxisCompatTessellationFailure{};
    std::uint64_t praxisCompatVertices{};
    std::uint64_t praxisCompatUvRemappedVertices{};
    std::uint64_t praxisCompatUvRemapFailures{};
    std::uint64_t praxisCompatVerticesBeforeCull{};
    std::uint64_t praxisCompatVerticesCulled{};
    std::uint64_t praxisCompatVerticesAfterCull{};
    std::uint64_t praxisCompatFacePairsCulled{};
    std::uint64_t praxisCompatCullSkipped{};
    std::uint64_t praxisCompatDerivedColorVertices{};
    std::uint64_t praxisCompatBuildSections{};
    std::uint64_t praxisCompatCapturedPositions{};
    std::uint64_t praxisCompatCapturedNormals{};
    std::uint64_t praxisCompatCapturedTangents{};
    std::uint64_t praxisCompatCapturedColors{};
    std::uint64_t praxisCompatCapturedBoneIds{};
    std::uint64_t praxisCompatCapturedUv0{};
    std::uint64_t praxisCompatCapturedUv1{};
    std::uint64_t praxisCompatCapturedUv2{};
    std::uint64_t praxisCompatCapturedPbrTextureIndices{};
    std::uint64_t praxisCompatCapturedMers{};
    std::uint64_t praxisCompatCapturedGeoType{};
    std::uint64_t praxisCompatCapturedQuadInfo{};
    std::uint64_t praxisCompatDoubleLiquidBuildSections{};
    std::uint64_t praxisCompatShaderColorWhite{};
    std::uint64_t praxisCompatSignTextResolved{};
    std::uint64_t praxisCompatTerrainTextureReady{};
    std::uint64_t praxisCompatImmediateSubmits{};
    std::uint64_t praxisCompatRetainedFallbackDraws{};
    std::uint64_t praxisCompatFullNativeStreamsPreserved{};
    std::uint64_t praxisCompatTextureRefSubmit{};
    std::uint64_t praxisCompatTerrainTextureBound{};
    std::uint64_t praxisCompatPerVertexReemit{};
    std::uint64_t praxisCompatImmediateSubmitsPerFrame{};
    std::uint64_t praxisCompatVerticesReplayedPerFrame{};
    std::uint64_t praxisCompatReplayMicros{};
    std::uint64_t praxisCompatSubmitMicros{};
    std::uint64_t praxisCompatAggregateBuilds{};
    std::uint64_t praxisLiquidBlendSignTextReady{};
    std::uint64_t praxisLiquidBlendSourceReady{};
    std::uint64_t praxisLiquidBlendOverrideApplied{};
    std::uint64_t praxisLiquidBlendRestored{};
    std::uint64_t praxisLiquidBlendStatesDiffer{};
    std::uint64_t nativeLiquidColorVertices{};
    std::uint64_t nativeLiquidAlphaModifiedVertices{};
    std::uint64_t virtualLiquidQueryHits{};
    std::uint64_t nativeLiquidSignTextResolved{};
    std::uint64_t nativeLiquidSignTextDraws{};
    std::uint64_t nativeLiquidTerrainBlendResolved{};
    std::uint64_t nativeLiquidTerrainBlendDraws{};
    std::uint64_t nativeLiquidLegacyMaterialDraws{};
    std::uint64_t liquidProxyFallbackCells{};
    std::uint64_t liquidProxyDrawCells{};
    std::array<std::uint64_t, 22> nativeLiquidLayerAttempts{};
};

struct ProjectedBlockActor {
    BlockPos     position{};
    Block const* block{};
    BlockActor*  actor{};
    std::size_t  structureIndex{};
};

using ExpectedBlockMap      = std::map<SubChunkKey, Block const*>;
using ExpectedLiquidMap     = std::map<SubChunkKey, Block const*>;
using ExpectedBlockActorMap = std::map<SubChunkKey, std::shared_ptr<BlockActor>>;
using ExpectedBlockIndexMap = std::map<SubChunkKey, std::size_t>;

} // namespace lholo::projection::detail
