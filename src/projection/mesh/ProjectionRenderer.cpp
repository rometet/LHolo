// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/mesh/ProjectionRenderer.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionLiquidCompatColor.h"
#include "projection/core/ProjectionLiquidFaceCull.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionVirtualWorld.h"
#include "plugin/LHolo.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <glm/common.hpp>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/ActorShaderManager.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/common/Brightness.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/core/renderer/RenderMaterialInfo.h"
#include "mc/deps/renderer/ShaderColor.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/RenderMaterial.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/hal/interface/DepthStencilStateDescription.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaRenderBlockActorComponent.h"
#include "ll/api/mod/NativeMod.h"

#include "render/OverlayMaterials.h"

namespace lholo::projection::detail {

namespace {

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

std::atomic_bool gSignTextResolvedLogged{};
std::atomic_bool gSignTextUnavailableLogged{};
std::atomic_bool gPraxisExactReplayLogged{};
std::atomic_bool gPraxisLiquidMaterialParityLogged{};

enum class PraxisLiquidMaterialCandidate : std::uint8_t {
    SignText,
    BlendBlock
};

// Candidate A remains available as an explicit diagnostic build. Phase 4B
// uses Candidate B and changes only the MaterialPtr passed to Exact Replay.
#if defined(LHOLO_PRAXIS_LIQUID_SIGN_TEXT_DIAGNOSTIC)
inline constexpr PraxisLiquidMaterialCandidate ActivePraxisLiquidMaterial =
    PraxisLiquidMaterialCandidate::SignText;
#else
inline constexpr PraxisLiquidMaterialCandidate ActivePraxisLiquidMaterial =
    PraxisLiquidMaterialCandidate::BlendBlock;
#endif

bool materialExists(mce::MaterialPtr const& material) {
    return material.mRenderMaterialInfoPtr.get() != nullptr;
}

mce::RenderMaterial* tryRenderMaterial(mce::MaterialPtr const& material) {
    auto* info = material.mRenderMaterialInfoPtr.get();
    return info ? info->mPtr.get() : nullptr;
}

OffscreenCaptureDescription const& emptyOffscreenCaptureDescription() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

// The correction shells carry no real UVs, but their overlay materials still
// sample texture slot 0 for the alpha test: an empty variant leaves the
// missing-texture checkerboard bound there, so lookups depended on stale slot
// state. The white texture comes from render/OverlayMaterials.h; the section
// builder aims every overlay vertex at its center so the sampler reads exactly
// (1,1,1,1) and never discards.

// Temporarily turns off depth testing on a shared render material so correction
// overlay geometry draws through world blocks (X-ray), restoring it when the scope ends.
// Safe because projection rendering runs synchronously on the present thread and
// vanilla never draws between the set and the restore.
class ScopedNoDepthTest {
public:
    ScopedNoDepthTest(mce::MaterialPtr const& material, bool enable) {
        if (!enable || !materialExists(material)) return;
        mMaterial = tryRenderMaterial(material);
        if (!mMaterial) return;
        auto& description = mMaterial->depthStencilStateDescription.get();
        mSaved = description.depthTestEnabled;
        description.depthTestEnabled = false;
    }
    ~ScopedNoDepthTest() {
        if (!mMaterial) return;
        mMaterial->depthStencilStateDescription.get().depthTestEnabled = mSaved;
    }
    ScopedNoDepthTest(ScopedNoDepthTest const&) = delete;
    ScopedNoDepthTest& operator=(ScopedNoDepthTest const&) = delete;

private:
    mce::RenderMaterial* mMaterial{};
    bool                 mSaved{};
};

class ScopedShaderColorWhite {
public:
    explicit ScopedShaderColorWhite(ScreenContext& screenContext)
        : mShader(screenContext.currentShaderColor),
          mSaved(mShader.color.get()) {
        mShader.color.get() = mce::Color{1.0F, 1.0F, 1.0F, 1.0F};
        mShader.dirty = true;
    }

    ~ScopedShaderColorWhite() {
        mShader.color.get() = mSaved;
        // The immediate submit may have uploaded white. Mark the restored
        // value dirty so the next vanilla owner cannot inherit that multiplier.
        mShader.dirty = true;
    }

    ScopedShaderColorWhite(ScopedShaderColorWhite const&) = delete;
    ScopedShaderColorWhite& operator=(ScopedShaderColorWhite const&) = delete;

private:
    ShaderColor& mShader;
    mce::Color   mSaved;
};

template <class Value>
bool appendMatchingNativeField(
    std::vector<Value>&       destination,
    std::vector<Value> const& source
) {
    if (destination.empty() != source.empty()) return false;
    destination.insert(destination.end(), source.begin(), source.end());
    return true;
}

bool appendPraxisExactReplayStream(
    PraxisCompatLiquidSectionData&       destination,
    PraxisCompatLiquidSectionData const& source
) {
    if (!destination.ready() || !source.ready()) return false;
    auto& destinationData = *destination.nativeStream;
    auto const& sourceData = *source.nativeStream;
    if (destinationData.mMode != sourceData.mMode
        || destinationData.mFieldEnabled.get() != sourceData.mFieldEnabled.get()
        || destination.tessellatorState.isFormatFixed
            != source.tessellatorState.isFormatFixed
        || destination.tessellatorState.hasNormals
            != source.tessellatorState.hasNormals
        || destination.tessellatorState.indexPhase
            != source.tessellatorState.indexPhase
        || destination.tessellatorState.noColor
            != source.tessellatorState.noColor
        || destination.tessellatorState.buildFaceData
            != source.tessellatorState.buildFaceData
        || destination.tessellatorState.quadInfo.empty()
            != source.tessellatorState.quadInfo.empty()) {
        return false;
    }

    destinationData.mPositions.get().insert(
        destinationData.mPositions.get().end(),
        sourceData.mPositions.get().begin(),
        sourceData.mPositions.get().end()
    );
    if (!appendMatchingNativeField(
            destinationData.mNormals.get(), sourceData.mNormals.get()
        )
        || !appendMatchingNativeField(
            destinationData.mTangents.get(), sourceData.mTangents.get()
        )
        || !appendMatchingNativeField(
            destinationData.mColors.get(), sourceData.mColors.get()
        )
        || !appendMatchingNativeField(
            destinationData.mBoneId0s.get(), sourceData.mBoneId0s.get()
        )
        || !appendMatchingNativeField(
            destinationData.mTextureUVs[0].get(), sourceData.mTextureUVs[0].get()
        )
        || !appendMatchingNativeField(
            destinationData.mTextureUVs[1].get(), sourceData.mTextureUVs[1].get()
        )
        || !appendMatchingNativeField(
            destinationData.mTextureUVs[2].get(), sourceData.mTextureUVs[2].get()
        )
        || !appendMatchingNativeField(
            destinationData.mPBRTextureIndices.get(),
            sourceData.mPBRTextureIndices.get()
        )
        || !appendMatchingNativeField(
            destinationData.mMERS.get(), sourceData.mMERS.get()
        )
        || !appendMatchingNativeField(
            destinationData.mGeoType.get(), sourceData.mGeoType.get()
        )) {
        return false;
    }
    destination.derivedColors.insert(
        destination.derivedColors.end(),
        source.derivedColors.begin(),
        source.derivedColors.end()
    );
    destination.liquidKinds.insert(
        destination.liquidKinds.end(),
        source.liquidKinds.begin(),
        source.liquidKinds.end()
    );
    destination.tessellatorState.quadInfo.insert(
        destination.tessellatorState.quadInfo.end(),
        source.tessellatorState.quadInfo.begin(),
        source.tessellatorState.quadInfo.end()
    );
    destination.tessellatorState.count = static_cast<std::uint32_t>(
        destinationData.mPositions.get().size()
    );
    destination.tessellatorState.maxVertexCount = std::max(
        destination.tessellatorState.count,
        destination.tessellatorState.maxVertexCount
            + source.tessellatorState.maxVertexCount
    );
    return destination.ready();
}

void refreshPraxisExactReplayBounds(PraxisCompatLiquidSectionData& data) {
    auto& meshData = *data.nativeStream;
    auto const& positions = meshData.mPositions.get();
    auto minimum = positions.front();
    auto maximum = positions.front();
    for (auto const& position : positions) {
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    meshData.mAABB.get() = {minimum, maximum};

    auto const& uv0 = meshData.mTextureUVs[0].get();
    auto uvMinimum = uv0.front();
    auto uvMaximum = uv0.front();
    for (auto const& uv : uv0) {
        uvMinimum = glm::min(uvMinimum, uv);
        uvMaximum = glm::max(uvMaximum, uv);
    }
    meshData.mUVAABB.get() = {uvMinimum, uvMaximum};
}

template <class Value>
void compactPraxisAggregatePerVertexField(
    std::vector<Value>&                 field,
    std::span<std::uint8_t const>       removeQuads
) {
    if (field.empty()) return;
    std::size_t write = 0;
    for (std::size_t quad = 0; quad < removeQuads.size(); ++quad) {
        if (removeQuads[quad] != 0U) continue;
        auto const read = quad * 4U;
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            if (write != read + corner) {
                field[write] = std::move(field[read + corner]);
            }
            ++write;
        }
    }
    field.resize(write);
}

template <class Value>
void compactPraxisAggregatePerQuadField(
    std::vector<Value>&                 field,
    std::span<std::uint8_t const>       removeQuads
) {
    if (field.empty()) return;
    std::size_t write = 0;
    for (std::size_t quad = 0; quad < removeQuads.size(); ++quad) {
        if (removeQuads[quad] != 0U) continue;
        if (write != quad) field[write] = std::move(field[quad]);
        ++write;
    }
    field.resize(write);
}

std::unique_ptr<PraxisCompatLiquidSectionData> clonePraxisExactReplayData(
    PraxisCompatLiquidSectionData const& source
) {
    if (!source.nativeStream) return {};
    auto clone = std::make_unique<PraxisCompatLiquidSectionData>();
    clone->nativeStream = std::make_unique<mce::MeshData>(*source.nativeStream);
    clone->derivedColors = source.derivedColors;
    clone->liquidKinds = source.liquidKinds;
    clone->tessellatorState = source.tessellatorState;
    return clone;
}

struct PraxisAggregateBoundaryCullResult {
    bool        valid{};
    std::size_t verticesBefore{};
    std::size_t verticesCulled{};
    std::size_t verticesAfter{};
    std::size_t facePairsCulled{};
};

PraxisAggregateBoundaryCullResult cullPraxisAggregateBoundaryFaces(
    PraxisCompatLiquidSectionData& data
) {
    PraxisAggregateBoundaryCullResult result{};
    if (!data.ready()) return result;

    auto& meshData = *data.nativeStream;
    auto& positions = meshData.mPositions.get();
    result.verticesBefore = positions.size();
    result.verticesAfter = result.verticesBefore;
    if (!meshData.mIndices.get().empty()) return result;

    std::array<std::size_t, 10> const fieldCounts{
        meshData.mNormals.get().size(),
        meshData.mTangents.get().size(),
        meshData.mColors.get().size(),
        meshData.mBoneId0s.get().size(),
        meshData.mTextureUVs[0].get().size(),
        meshData.mTextureUVs[1].get().size(),
        meshData.mTextureUVs[2].get().size(),
        meshData.mPBRTextureIndices.get().size(),
        meshData.mMERS.get().size(),
        meshData.mGeoType.get().size()
    };
    auto const vertexCount = positions.size();
    auto const quadCount = vertexCount / 4U;
    if (!nativeLiquidPerVertexFieldCountsMatch(vertexCount, fieldCounts)
        || data.derivedColors.size() != vertexCount
        || data.liquidKinds.size() != vertexCount
        || (!data.tessellatorState.quadInfo.empty()
            && data.tessellatorState.quadInfo.size() != quadCount)) {
        return result;
    }

    auto const cullMask = buildNativeLiquidInternalFaceCullMask(
        std::span<glm::vec3 const>{positions.data(), positions.size()}
    );
    if (!cullMask.valid || cullMask.removeQuads.size() != quadCount
        || cullMask.removedVertices() >= vertexCount) {
        return result;
    }

    compactPraxisAggregatePerVertexField(positions, cullMask.removeQuads);
    compactPraxisAggregatePerVertexField(
        meshData.mNormals.get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        meshData.mTangents.get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        meshData.mColors.get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        meshData.mBoneId0s.get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        meshData.mTextureUVs[0].get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        meshData.mTextureUVs[1].get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        meshData.mTextureUVs[2].get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        meshData.mPBRTextureIndices.get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        meshData.mMERS.get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        meshData.mGeoType.get(), cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        data.derivedColors, cullMask.removeQuads
    );
    compactPraxisAggregatePerVertexField(
        data.liquidKinds, cullMask.removeQuads
    );
    compactPraxisAggregatePerQuadField(
        data.tessellatorState.quadInfo, cullMask.removeQuads
    );

    auto const finalVertexCount = positions.size();
    if (finalVertexCount > std::numeric_limits<std::uint32_t>::max()) {
        return result;
    }
    data.tessellatorState.count = static_cast<std::uint32_t>(finalVertexCount);
    data.tessellatorState.maxVertexCount =
        static_cast<std::uint32_t>(finalVertexCount);
    refreshPraxisExactReplayBounds(data);
    if (!data.ready()) return result;

    result.valid = true;
    result.verticesCulled = cullMask.removedVertices();
    result.verticesAfter = finalVertexCount;
    result.facePairsCulled = cullMask.facePairs;
    return result;
}

std::unique_ptr<PraxisCompatLiquidSectionData> buildPraxisExactReplayAggregate(
    ProjectionState&                   state,
    std::span<std::size_t const>       sections
) {
    std::unique_ptr<PraxisCompatLiquidSectionData> result;
    for (auto const section : sections) {
        if (section >= state.praxisCompatLiquidSections.size()) return {};
        auto const& source = state.praxisCompatLiquidSections[section];
        if (!source || !source->ready()) return {};
        if (!result) {
            result = std::make_unique<PraxisCompatLiquidSectionData>();
            result->nativeStream = std::make_unique<mce::MeshData>(
                *source->nativeStream
            );
            result->derivedColors = source->derivedColors;
            result->liquidKinds = source->liquidKinds;
            result->tessellatorState = source->tessellatorState;
        } else if (!appendPraxisExactReplayStream(*result, *source)) {
            return {};
        }
    }
    if (!result || !result->ready()) return {};

    auto candidate = clonePraxisExactReplayData(*result);
    auto const boundaryCull = candidate
        ? cullPraxisAggregateBoundaryFaces(*candidate)
        : PraxisAggregateBoundaryCullResult{};
    auto& telemetry = state.nativeLiquidTelemetry;
    telemetry.praxisCompatAggregateVerticesBeforeBoundaryCull =
        result->nativeStream->mPositions.get().size();
    if (boundaryCull.valid && candidate && candidate->ready()) {
        telemetry.praxisCompatAggregateVerticesBeforeBoundaryCull =
            boundaryCull.verticesBefore;
        telemetry.praxisCompatAggregateVerticesBoundaryCulled =
            boundaryCull.verticesCulled;
        telemetry.praxisCompatAggregateVerticesAfterBoundaryCull =
            boundaryCull.verticesAfter;
        telemetry.praxisCompatAggregateBoundaryFacePairsCulled =
            boundaryCull.facePairsCulled;
        telemetry.praxisCompatAggregateBoundaryCullSkipped = 0U;
        result = std::move(candidate);
    } else {
        telemetry.praxisCompatAggregateVerticesBoundaryCulled = 0U;
        telemetry.praxisCompatAggregateVerticesAfterBoundaryCull =
            telemetry.praxisCompatAggregateVerticesBeforeBoundaryCull;
        telemetry.praxisCompatAggregateBoundaryFacePairsCulled = 0U;
        telemetry.praxisCompatAggregateBoundaryCullSkipped = 1U;
        refreshPraxisExactReplayBounds(*result);
    }
    logger().info(
        "PRAXIS_LIQUID_BOUNDARY_CULL before={} culled={} after={} pairs={} sections={} skipped={}",
        telemetry.praxisCompatAggregateVerticesBeforeBoundaryCull,
        telemetry.praxisCompatAggregateVerticesBoundaryCulled,
        telemetry.praxisCompatAggregateVerticesAfterBoundaryCull,
        telemetry.praxisCompatAggregateBoundaryFacePairsCulled,
        sections.size(),
        telemetry.praxisCompatAggregateBoundaryCullSkipped
    );
    return result;
}

struct PraxisExactReplaySubmitResult {
    std::uint64_t vertices{};
    std::uint64_t replayMicros{};
    std::uint64_t submitMicros{};
};

PraxisExactReplaySubmitResult submitPraxisExactReplayImmediately(
    ScreenContext&                       screenContext,
    PraxisCompatLiquidSectionData const& data,
    mce::MaterialPtr const&              material,
    mce::TexturePtr const&               terrainTexture
) {
    PraxisExactReplaySubmitResult result{};
    auto const replayStarted = std::chrono::steady_clock::now();
    Tessellator& tessellator = screenContext.tessellator;
    auto const vertexCount = data.nativeStream->mPositions.get().size();
    tessellator.begin(
        Tessellator::DebugContextCallback{},
        mce::PrimitiveMode::QuadList,
        static_cast<int>(vertexCount),
        true
    );
    mce::MeshData replayData{*data.nativeStream};
    replayData.mColors.get() = data.derivedColors;
    tessellator.mMeshData.get() = std::move(replayData);
    tessellator.mIsFormatFixed = data.tessellatorState.isFormatFixed;
    tessellator.mHasNormals = data.tessellatorState.hasNormals;
    tessellator.mIndexPhase = data.tessellatorState.indexPhase;
    tessellator.mNoColor = data.tessellatorState.noColor;
    tessellator.mBuildFaceData = data.tessellatorState.buildFaceData;
    tessellator.mQuadFacing = data.tessellatorState.quadFacing;
    tessellator.mQuadTwoSided = data.tessellatorState.quadTwoSided;
    tessellator.mCurQuadVertex = data.tessellatorState.curQuadVertex;
    tessellator.mCount = static_cast<std::uint32_t>(vertexCount);
    tessellator.mMaxVertexCount = std::max(
        static_cast<std::uint32_t>(vertexCount),
        data.tessellatorState.maxVertexCount
    );
    tessellator.mFaceCenterAccumulator =
        data.tessellatorState.faceCenterAccumulator;
    tessellator.mQuadInfoList.get() = data.tessellatorState.quadInfo;
    result.vertices = vertexCount;
    result.replayMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - replayStarted
        ).count()
    );

    // This explicit TexturePtr reference-list overload is the typed 26.51
    // equivalent of the texture-ref MeshHelpers entry used by old Praxis. It
    // is a different exported overload from TextureVariant + generation mode.
    auto const submitStarted = std::chrono::steady_clock::now();
    MeshHelpers::renderMeshImmediately(
        screenContext,
        tessellator,
        material,
        {std::cref(terrainTexture)},
        emptyOffscreenCaptureDescription()
    );
    result.submitMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - submitStarted
        ).count()
    );
    return result;
}

} // namespace

void submitProjectedBlockActorPass(
    ProjectionState&        state,
    BaseActorRenderContext& renderContext,
    BlockSource&            region,
    Vec3 const&             camera,
    bool                    renderAlphaLayer
) {
    if (state.projectedBlockActors.empty()) return;

    alignas(mce::MaterialPtr) static const std::byte sNoForcedMaterialStorage[sizeof(mce::MaterialPtr)]{};
    auto const& noForcedMaterial = *reinterpret_cast<mce::MaterialPtr const*>(sNoForcedMaterialStorage);
    auto& dispatcher = renderContext.mBlockEntityRenderDispatcher;
    ScopedTessellationBlocks blockActorWorldScope(
        *state.expectedWorldBlocks,
        *state.expectedWorldLiquids,
        *state.expectedWorldBlockActors
    );
    for (auto const& projected : state.projectedBlockActors) {
        auto const correctionState = state.correctionStates[projected.structureIndex];
        auto* renderComponent = projected.actor->_getRenderComponent();
        if (correctionState == CorrectionState::Correct
            || correctionState == CorrectionState::WrongType
            || correctionState == CorrectionState::WrongState
            || !renderComponent
            || !renderComponent->isWithinRenderDistance(camera)) {
            continue;
        }
        dispatcher.render(
            renderContext,
            region,
            *renderComponent,
            *projected.block,
            renderAlphaLayer,
            noForcedMaterial,
            nullptr,
            -1,
            std::nullopt
        );
    }
}

void submitProjectionMeshPass(
    ProjectionState&        state,
    BaseActorRenderContext& renderContext,
    IClientInstance&        client,
    BlockPos const&         renderOrigin,
    Vec3 const&             camera,
    float                   structureOpacity,
    bool                    renderAlphaLayer,
    bool                    structureBoundsEnabled,
    bool                    correctionSeeThrough,
    bool                    missingSeeThrough
) {
    auto& itemRenderer = renderContext.mItemInHandRenderer;
    auto const& blendMaterial = itemRenderer.mMatBlendBlock.get();

    auto worldCenter = [&](std::size_t section) {
        return Vec3{
            static_cast<float>(renderOrigin.x) + state.sections[section].center.x,
            static_cast<float>(renderOrigin.y) + state.sections[section].center.y,
            static_cast<float>(renderOrigin.z) + state.sections[section].center.z
        };
    };
    auto distanceSquared = [&](Vec3 const& point) {
        auto const dx = point.x - camera.x;
        auto const dy = point.y - camera.y;
        auto const dz = point.z - camera.z;
        return dx * dx + dy * dy + dz * dz;
    };

    // Every translucent projection path needs the same back-to-front section
    // order. Build it once in the alpha pass and reuse it for native liquids,
    // normal blend meshes and liquid proxies instead of sorting each list.
    std::vector<float> sectionDistances;
    std::vector<std::size_t> backToFrontSections;
    if (renderAlphaLayer) {
        sectionDistances.resize(state.sections.size());
        backToFrontSections.resize(state.sections.size());
        std::iota(backToFrontSections.begin(), backToFrontSections.end(), std::size_t{0});
        for (std::size_t section = 0; section < state.sections.size(); ++section) {
            sectionDistances[section] = distanceSquared(worldCenter(section));
        }
        std::sort(
            backToFrontSections.begin(),
            backToFrontSections.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                return sectionDistances[lhs] > sectionDistances[rhs];
            }
        );
    }

    // PraxisExactReplay preserves every typed native stream, replaces packed
    // color only, and submits all compatible sections as one texture-ref batch.
    // Retained meshes exist only for a section whose exact build failed, or in
    // the explicit retained diagnostic build.
    std::vector<std::size_t> nativeLiquidSections;
    bool nativeLiquidDrawnWithSelectedMaterial{};
    if (renderAlphaLayer) {
        auto& telemetry = state.nativeLiquidTelemetry;
        telemetry.praxisCompatImmediateSubmitsPerFrame = 0;
        telemetry.praxisCompatVerticesReplayedPerFrame = 0;
        telemetry.praxisCompatReplayMicros = 0;
        telemetry.praxisCompatSubmitMicros = 0;
        for (auto const section : backToFrontSections) {
            if (section >= state.nativeLiquidSectionMeshes.size()) continue;
            auto const& mesh = state.nativeLiquidSectionMeshes[section];
            auto const* compat = section < state.praxisCompatLiquidSections.size()
                ? state.praxisCompatLiquidSections[section].get()
                : nullptr;
            if ((mesh && mesh->isValid()) || (compat && compat->ready())) {
                nativeLiquidSections.push_back(section);
            }
        }
        if (!nativeLiquidSections.empty()) {
            if (auto const* signText = render::resolveSignTextMaterial()) {
                auto const signTextReady = tryRenderMaterial(*signText) != nullptr;
                auto const blendMaterialReady =
                    tryRenderMaterial(blendMaterial) != nullptr;
                auto const& exactReplayMaterial =
                    ActivePraxisLiquidMaterial
                            == PraxisLiquidMaterialCandidate::BlendBlock
                        ? blendMaterial
                        : *signText;
                auto const exactReplayMaterialReady =
                    tryRenderMaterial(exactReplayMaterial) != nullptr;
                telemetry.nativeLiquidSignTextResolved = signTextReady ? 1U : 0U;
                telemetry.praxisCompatSignTextResolved = signTextReady ? 1U : 0U;
                telemetry.praxisLiquidMaterialSignTextReady =
                    signTextReady ? 1U : 0U;
                telemetry.praxisLiquidMaterialBlendReady =
                    blendMaterialReady ? 1U : 0U;
                telemetry.praxisLiquidMaterialCandidateBlendBlock =
                    ActivePraxisLiquidMaterial
                            == PraxisLiquidMaterialCandidate::BlendBlock
                        ? 1U
                        : 0U;
                if (!gSignTextResolvedLogged.exchange(true, std::memory_order_acq_rel)) {
                    logger().info("NATIVE_LIQUID_SIGN_TEXT_RESOLVED material=sign_text");
                }
                telemetry.praxisCompatTerrainTextureReady =
                    state.terrainTexture ? 1U : 0U;
                auto const usePraxisExactReplay =
                    ActiveNativeLiquidRenderPath == NativeLiquidRenderPath::PraxisCompat
                    && state.terrainTexture.has_value()
                    && exactReplayMaterialReady;
                std::vector<std::size_t> exactSections;
                std::vector<std::size_t> retainedSections;
                for (auto const section : nativeLiquidSections) {
                    auto const& compat = state.praxisCompatLiquidSections[section];
                    if (usePraxisExactReplay && compat && compat->ready()) {
                        exactSections.push_back(section);
                    } else {
                        retainedSections.push_back(section);
                    }
                }

                if (!exactSections.empty()) {
                    if (state.praxisCompatLiquidAggregateDirty
                        || state.praxisCompatLiquidAggregateOrder != exactSections) {
                        auto const aggregateStarted = std::chrono::steady_clock::now();
                        state.praxisCompatLiquidAggregate =
                            buildPraxisExactReplayAggregate(state, exactSections);
                        state.praxisCompatLiquidAggregateOrder = exactSections;
                        state.praxisCompatLiquidAggregateDirty = false;
                        ++telemetry.praxisCompatAggregateBuilds;
                        telemetry.praxisCompatReplayMicros +=
                            static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - aggregateStarted
                                ).count()
                            );
                    }

                    auto submitExact = [&](PraxisCompatLiquidSectionData const& data) {
                        ScopedShaderColorWhite shaderColorWhite{
                            renderContext.mScreenContext
                        };
                        telemetry.praxisCompatShaderColorWhite = 1;
                        auto const submitted = submitPraxisExactReplayImmediately(
                            renderContext.mScreenContext,
                            data,
                            exactReplayMaterial,
                            *state.terrainTexture
                        );
                        ++telemetry.praxisCompatImmediateSubmits;
                        ++telemetry.praxisCompatImmediateSubmitsPerFrame;
                        telemetry.praxisCompatVerticesReplayedPerFrame
                            += submitted.vertices;
                        telemetry.praxisCompatReplayMicros += submitted.replayMicros;
                        telemetry.praxisCompatSubmitMicros += submitted.submitMicros;
                        telemetry.praxisCompatFullNativeStreamsPreserved = 1;
                        telemetry.praxisCompatTextureRefSubmit = 1;
                        telemetry.praxisCompatTerrainTextureBound = 1;
                        nativeLiquidDrawnWithSelectedMaterial = true;

                        if (!gPraxisLiquidMaterialParityLogged.exchange(
                                true,
                                std::memory_order_acq_rel
                            )) {
                            logger().info(
                                "PRAXIS_LIQUID_MATERIAL_PARITY candidate={} signTextReady={} blendMaterialReady={} textureRefSubmit=1 waterVertexAlpha={} lavaVertexAlpha=255 depthStateChanged=0 submitPerFrame={}",
                                ActivePraxisLiquidMaterial
                                        == PraxisLiquidMaterialCandidate::BlendBlock
                                    ? "mMatBlendBlock"
                                    : "sign_text",
                                signTextReady ? 1 : 0,
                                blendMaterialReady ? 1 : 0,
                                PraxisWaterDerivedAlpha,
                                telemetry.praxisCompatImmediateSubmitsPerFrame
                            );
                        }
                    };

                    if (state.praxisCompatLiquidAggregate
                        && state.praxisCompatLiquidAggregate->ready()) {
                        submitExact(*state.praxisCompatLiquidAggregate);
                    } else {
                        // Cross-section stream layouts can theoretically differ.
                        // Fall back to one bulk typed stream copy per section,
                        // never to per-vertex color/tex2/vertex re-emission.
                        for (auto const section : exactSections) {
                            submitExact(*state.praxisCompatLiquidSections[section]);
                        }
                    }
                }

                for (auto const section : retainedSections) {
                    auto const& mesh = state.nativeLiquidSectionMeshes[section];
                    if (!mesh || !mesh->isValid()) continue;
                    mesh->renderMesh(
                        renderContext.mScreenContext,
                        *signText,
                        *state.terrainTextureVariant,
                        0,
                        mesh->mVertexCount.get().value_or(0u),
                        emptyOffscreenCaptureDescription(),
                        nullptr
                    );
                    ++telemetry.nativeLiquidSignTextDraws;
                    if (ActiveNativeLiquidRenderPath == NativeLiquidRenderPath::PraxisCompat) {
                        ++telemetry.praxisCompatRetainedFallbackDraws;
                    }
                    nativeLiquidDrawnWithSelectedMaterial = true;
                }

                if (!exactSections.empty()
                    && !gPraxisExactReplayLogged.exchange(
                        true,
                        std::memory_order_acq_rel
                    )) {
                    logger().info(
                        "PRAXIS_EXACT_REPLAY path=PraxisExactReplay generationBeginFlag=1 tessellateFlag=0 layer=3 uvRemapped={} culled={} derivedColors={} fullNativeStreamsPreserved={} textureRefSubmit={} terrainTextureBound={} perVertexReemit={} doubleLiquidBuild={} shaderColorWhite={} submitPath=MeshHelpers::renderMeshImmediately(texture-refs) material={} submitPerFrame={} verticesReplayed={} replayMicros={} submitMicros={} retainedFallbackDraws={}",
                        telemetry.praxisCompatUvRemappedVertices,
                        telemetry.praxisCompatVerticesCulled,
                        telemetry.praxisCompatDerivedColorVertices,
                        telemetry.praxisCompatFullNativeStreamsPreserved,
                        telemetry.praxisCompatTextureRefSubmit,
                        telemetry.praxisCompatTerrainTextureBound,
                        telemetry.praxisCompatPerVertexReemit,
                        telemetry.praxisCompatDoubleLiquidBuildSections,
                        telemetry.praxisCompatShaderColorWhite,
                        ActivePraxisLiquidMaterial
                                == PraxisLiquidMaterialCandidate::BlendBlock
                            ? "mMatBlendBlock"
                            : "sign_text",
                        telemetry.praxisCompatImmediateSubmitsPerFrame,
                        telemetry.praxisCompatVerticesReplayedPerFrame,
                        telemetry.praxisCompatReplayMicros,
                        telemetry.praxisCompatSubmitMicros,
                        telemetry.praxisCompatRetainedFallbackDraws
                    );
                }
            } else if (!gSignTextUnavailableLogged.exchange(
                           true,
                           std::memory_order_acq_rel
                       )) {
                logger().warn("NATIVE_LIQUID_SIGN_TEXT_UNAVAILABLE material=sign_text");
            }
        }
    }

    // The ItemInHand/Entity materials used for ghost blocks drive their
    // diffuse lighting from TileLightColor in constant buffer CB0. When
    // looking down at the ground with no block entities in the view frustum
    // (especially in single-layer mode), CB0 retains stale or zeroed lighting
    // from selection outlines, turning the projection black. Explicitly
    // prime actor constants with full brightness (Brightness::MAX()) so
    // projection meshes stay consistently illuminated in all view angles.
    if (auto* player = client.getLocalPlayer()) {
        ActorShaderManager::setupShaderParameters(
            renderContext.mScreenContext,
            renderContext,
            *player,
            mce::Color{1.0f, 1.0f, 1.0f, 0.0f},
            1.0f,
            Brightness::MAX(),
            std::nullopt
        );
    }

    struct VisibleMesh {
        std::size_t bucket;
        std::size_t section;
    };
    auto renderMeshes = [&](std::vector<VisibleMesh> const& meshes, mce::MaterialPtr const& material) {
        if (!materialExists(material)) return;
        for (auto const& visible : meshes) {
            auto& mesh = *state.sections[visible.section].meshes[visible.bucket];
            mesh.renderMesh(
                renderContext.mScreenContext,
                material,
                *state.terrainTextureVariant,
                0,
                mesh.mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
        }
    };

    auto const opaqueBucket = static_cast<std::size_t>(RenderBucket::Opaque);
    auto const alphaBucket = static_cast<std::size_t>(RenderBucket::Alpha);
    auto const alphaOneSidedBucket = static_cast<std::size_t>(RenderBucket::AlphaOneSided);
    auto const blendBucket = static_cast<std::size_t>(RenderBucket::Blend);
    constexpr auto bucketCount = static_cast<std::size_t>(RenderBucket::Count);
    std::array<std::vector<VisibleMesh>, bucketCount> visibleByBucket;
    for (auto& bucket : visibleByBucket) bucket.reserve(state.sections.size() / bucketCount + 1U);
    for (std::size_t section = 0; section < state.sections.size(); ++section) {
        for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
            auto const& mesh = state.sections[section].meshes[bucket];
            if (mesh && mesh->isValid()) {
                visibleByBucket[bucket].push_back({bucket, section});
            }
        }
    }

    if (structureOpacity >= 0.999f) {
        auto& opaqueMeshes = visibleByBucket[opaqueBucket];
        auto& alphaMeshes = visibleByBucket[alphaBucket];
        auto& alphaOneSidedMeshes = visibleByBucket[alphaOneSidedBucket];

        // Biome-tinted blocks (leaves, grass tops) carry their color in vertex
        // data, but the plain block materials' shaders have no COLOR input on
        // 26.40, which rendered leaves as the raw grayscale texture (white).
        // The engine's Colored block materials read COLOR0 and keep the tint;
        // fall back to the plain ones if unavailable.
        auto const& opaqueMaterial = materialExists(itemRenderer.mMatOpaqueBlockColor.get())
            ? itemRenderer.mMatOpaqueBlockColor.get()
            : itemRenderer.mMatOpaqueBlock.get();
        auto const& alphaMaterial = materialExists(itemRenderer.mMatAlphaColoredBlock.get())
            ? itemRenderer.mMatAlphaColoredBlock.get()
            : itemRenderer.mMatAlphaBlock.get();
        auto const& alphaOneSidedMaterial = materialExists(itemRenderer.mMatAlphaOneSidedColoredBlock.get())
            ? itemRenderer.mMatAlphaOneSidedColoredBlock.get()
            : itemRenderer.mMatAlphaOneSidedBlock.get();
        if (!renderAlphaLayer) {
            renderMeshes(
                opaqueMeshes,
                materialExists(opaqueMaterial) ? opaqueMaterial : blendMaterial
            );
            renderMeshes(
                alphaMeshes,
                materialExists(alphaMaterial) ? alphaMaterial : blendMaterial
            );
            renderMeshes(
                alphaOneSidedMeshes,
                materialExists(alphaOneSidedMaterial)
                    ? alphaOneSidedMaterial
                    : (materialExists(alphaMaterial) ? alphaMaterial : blendMaterial)
            );
        } else {
            std::vector<VisibleMesh> transparentMeshes;
            transparentMeshes.reserve(visibleByBucket[blendBucket].size());
            for (auto const section : backToFrontSections) {
                auto const& mesh = state.sections[section].meshes[blendBucket];
                if (mesh && mesh->isValid()) {
                    transparentMeshes.push_back({blendBucket, section});
                }
            }
            renderMeshes(transparentMeshes, blendMaterial);
        }
    } else if (renderAlphaLayer) {
        // True projection transparency needs a blending material even for
        // normally opaque/cutout blocks. Reuse the common section order.
        std::vector<VisibleMesh> transparentMeshes;
        std::size_t transparentMeshCount{};
        for (auto const& bucket : visibleByBucket) transparentMeshCount += bucket.size();
        transparentMeshes.reserve(transparentMeshCount);
        for (auto const section : backToFrontSections) {
            for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
                auto const& mesh = state.sections[section].meshes[bucket];
                if (mesh && mesh->isValid()) {
                    transparentMeshes.push_back({bucket, section});
                }
            }
        }
        renderMeshes(transparentMeshes, blendMaterial);
    }

    // If the selected Exact Replay material is unavailable, retain the legacy
    // mesh fallback. Candidate A still requires exact sign_text resolution;
    // Candidate B requires the typed mMatBlendBlock MaterialPtr instead.
    if (renderAlphaLayer && !nativeLiquidDrawnWithSelectedMaterial) {
        for (auto const section : nativeLiquidSections) {
            auto const& mesh = state.nativeLiquidSectionMeshes[section];
            if (!mesh || !mesh->isValid()) continue;
            mesh->renderMesh(
                renderContext.mScreenContext,
                blendMaterial,
                *state.terrainTextureVariant,
                0,
                mesh->mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
            ++state.nativeLiquidTelemetry.nativeLiquidLegacyMaterialDraws;
            if (ActiveNativeLiquidRenderPath == NativeLiquidRenderPath::PraxisCompat) {
                ++state.nativeLiquidTelemetry.praxisCompatRetainedFallbackDraws;
            }
        }
    }

    // Textured liquid hulls travel the proven glass path: blend-block material
    // plus the terrain atlas, sorted back to front by section.
    if (renderAlphaLayer) {
        for (auto const liquidSection : backToFrontSections) {
            if (liquidSection >= state.liquidProxySectionMeshes.size()) continue;
            auto const& proxyMesh = state.liquidProxySectionMeshes[liquidSection];
            if (!proxyMesh || !proxyMesh->isValid()) continue;
            auto& mesh = *proxyMesh;
            mesh.renderMesh(
                renderContext.mScreenContext,
                blendMaterial,
                *state.terrainTextureVariant,
                0,
                mesh.mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
            state.nativeLiquidTelemetry.liquidProxyDrawCells
                += state.liquidProxySectionCellCounts[liquidSection];
        }

        // Textured placeholder hulls for block-entity blocks.
        for (auto const& placeholder : state.blockEntityPlaceholderSectionMeshes) {
            if (!placeholder || !placeholder->isValid()) continue;
            placeholder->renderMesh(
                renderContext.mScreenContext,
                blendMaterial,
                *state.terrainTextureVariant,
                0,
                placeholder->mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
        }
    }

    if (!renderAlphaLayer) return;

    auto* levelRenderer = client.getLevelRenderer();
    auto const& outlineMaterial = levelRenderer
        ? levelRenderer->mLevelRendererPlayer->mOutlineSelectionMaterial.get()
        : itemRenderer.mMatBlendBlock.get();
    auto const overlayTexture = render::resolveWhiteTextureVariant(levelRenderer);
    // Prefer the glow sign text material for the bounds box: its shader reads
    // the vertex color the builder wrote (bright cyan) instead of the
    // engine-driven uniform color of the selection outline family.
    auto const* glowMaterial = render::resolveGlowSignMaterial();
    auto const& boundsMaterial = glowMaterial ? *glowMaterial : outlineMaterial;
    if (materialExists(boundsMaterial) && structureBoundsEnabled
        && state.structureBoundsMesh && state.structureBoundsMesh->isValid()) {
        state.structureBoundsMesh->renderMesh(
            renderContext.mScreenContext,
            boundsMaterial,
            overlayTexture,
            0,
            state.structureBoundsMesh->mVertexCount.get().value_or(0u),
            emptyOffscreenCaptureDescription(),
            nullptr
        );
    }
    auto const& warningMaterial = levelRenderer
        ? levelRenderer->mLevelRendererPlayer->selectionBlockEntityOverlayColorMaterial.get()
        : itemRenderer.mMatBlendBlockNoColor.get();
    // seeThroughMeshes is passed per call so the "missing" correction meshes stay
    // depth-tested while only the "wrong" ones honor the X-ray toggle.
    auto renderOverlayMeshes = [&] (
        std::vector<std::unique_ptr<mce::Mesh>> const& meshes,
        mce::MaterialPtr const& material,
        bool seeThroughMeshes
    ) {
        if (!materialExists(material)) return;
        ScopedNoDepthTest seeThrough(material, seeThroughMeshes);
        for (auto const& overlay : meshes) {
            if (!overlay || !overlay->isValid()) continue;
            overlay->renderMesh(
                renderContext.mScreenContext,
                material,
                overlayTexture,
                0,
                overlay->mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
        }
    };

    struct MaterialStateRestore {
        mce::RenderMaterial* material{};
        std::optional<mce::BlendStateDescription> blend;
        mce::PrimitiveMode primitive{};
        float depthBias{};
        float slopeBias{};
        bool restorePrimitive{};
        ~MaterialStateRestore() {
            if (!material || !blend) return;
            material->blendStateDescription.get() = *blend;
            material->mDepthBias = depthBias;
            material->mSlopeScaledDepthBias = slopeBias;
            if (restorePrimitive) material->mPrimitiveMode = primitive;
        }
    };

    if (static_cast<bool>(itemRenderer.mIsDeferredEnabled)) {
        // Vibrant Visuals: reuse the colored outline shader for the hull and
        // restore every temporary material field immediately after submission.
        auto* renderMaterial = tryRenderMaterial(outlineMaterial);
        MaterialStateRestore restore;
        auto* blendRenderMaterial = tryRenderMaterial(blendMaterial);
        if (renderMaterial && blendRenderMaterial) {
            restore.material = renderMaterial;
            restore.blend = renderMaterial->blendStateDescription.get();
            restore.primitive = renderMaterial->mPrimitiveMode;
            restore.depthBias = renderMaterial->mDepthBias;
            restore.slopeBias = renderMaterial->mSlopeScaledDepthBias;
            restore.restorePrimitive = true;
            renderMaterial->mPrimitiveMode = mce::PrimitiveMode::QuadList;
            renderMaterial->blendStateDescription.get()
                = blendRenderMaterial->blendStateDescription.get();
            renderMaterial->mDepthBias = 100.0f;
            renderMaterial->mSlopeScaledDepthBias = 15.0f;
            renderOverlayMeshes(state.warningFillSectionMeshes, outlineMaterial, missingSeeThrough);
            renderOverlayMeshes(state.wrongFillSectionMeshes, outlineMaterial, correctionSeeThrough);
        }
    } else if (materialExists(warningMaterial)) {
        // Vanilla selection overlay has the required depth bias. Temporarily
        // borrow SourceAlpha/OneMinusSourceAlpha from blend-block material.
        auto* renderMaterial = levelRenderer ? tryRenderMaterial(warningMaterial) : nullptr;
        MaterialStateRestore restore;
        auto* blendRenderMaterial = tryRenderMaterial(blendMaterial);
        if (renderMaterial && blendRenderMaterial) {
            restore.material = renderMaterial;
            restore.blend = renderMaterial->blendStateDescription.get();
            restore.depthBias = renderMaterial->mDepthBias;
            restore.slopeBias = renderMaterial->mSlopeScaledDepthBias;
            renderMaterial->blendStateDescription.get()
                = blendRenderMaterial->blendStateDescription.get();
            renderMaterial->mDepthBias = 100.0f;
            renderMaterial->mSlopeScaledDepthBias = 15.0f;
        }
        renderOverlayMeshes(state.warningFillSectionMeshes, warningMaterial, missingSeeThrough);
        renderOverlayMeshes(state.wrongFillSectionMeshes, warningMaterial, correctionSeeThrough);
    }
    if (materialExists(outlineMaterial)) {
        renderOverlayMeshes(state.correctionOutlineSectionMeshes, outlineMaterial, missingSeeThrough);
        renderOverlayMeshes(state.wrongOutlineSectionMeshes, outlineMaterial, correctionSeeThrough);
    }
}

} // namespace lholo::projection::detail
