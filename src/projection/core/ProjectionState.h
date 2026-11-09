// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Projection runtime state and resource ownership. Lifecycle operations remain
// in Projection.cpp; this type only makes the ownership boundary explicit.

#pragma once

#include "projection/correction/CorrectionScanProgress.h"
#include "projection/correction/CorrectionStateStore.h"
#include "projection/correction/CorrectionScanScheduler.h"
#include "projection/correction/CorrectionWorkQueues.h"
#include "projection/correction/CorrectionCellMask.h"
#include "projection/correction/CorrectionAirScan.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/world/ProjectionWorldLookup.h"
#include "projection/core/LiquidBoundaryMaskCache.h"
#include "projection/core/ProjectionVisibilityRules.h"
#include "projection/core/ProjectionDrawCache.h"
#include "projection/core/ProjectionResidencyScan.h"
#include "projection/mesh/ProjectionCorrectionBatchCache.h"
#include "projection/mesh/ProjectionTransparentBatchCache.h"
#include "projection/ProjectionTypes.h"
#include "projection/mesh/SectionBlockSnapshot.h"
#include "projection/mesh/SectionMissingLayerSnapshot.h"
#include "projection/core/ProjectionMissingLayers.h"
#include "projection/runtime/MeshDiagnosticGate.h"
#include "overlay/BoundsWireframe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <variant>
#include <vector>

#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/core/renderer/RenderMaterialInfo.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"

class Dimension;
class IClientInstance;
class Level;

namespace lholo::structure {
struct LoadedStructure;
}

namespace lholo::projection::detail {

using TextureVariant =
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;

struct MixedGlassLiquidMesh {
    // A source can disappear on any accepted section upload, world reload or
    // placement mutation. Use typed source identities + revision as the key;
    // native GPU buffers never outlive their matching section owners.
    void const* glassSource{};
    void const* liquidSource{};
    std::uint64_t uploadedRevision{};
    std::size_t vertexCount{};
    std::array<float, 3> axis{};
    bool axisValid{};
    std::vector<std::uint32_t> order;
    std::unique_ptr<mce::Mesh> mesh;
};
struct SectionState {
    // Conservative geometry AABB; fallback visibility stays true until layout
    // and camera matrices are ready.
    ProjectionBounds buildBounds{};
    ProjectionBounds meshBounds{};
    bool renderVisible{true};
    bool buildVisible{true};
    bool prefetchVisible{true};
    bool withinBuildDistance{true};
    bool withinRetentionDistance{true};
    Vec3    center{};
    bool    dirty{};
    bool    modelDirty{true};
    bool    incrementalDirty{};
    bool    interactiveDirty{};
    bool    buildInFlight{};
    std::uint64_t requestedRevision{};
    std::uint64_t uploadedRevision{};
    // The blend mesh is re-sorted only after the camera moves by a quarter
    // block. The mesh itself keeps projection-local coordinates.
    std::array<std::int64_t, 3> blendSortKey{};
    bool                        blendSortKeyValid{};
    bool                        blendSortUnsupported{};
    // Last successfully uploaded primitive ordering, not a second MeshData.
    // Allows camera movement without GPU re-upload if order did not change.
    std::vector<std::size_t>    blendSortOrder;
    std::uint64_t               blendSortUploadGeneration{};
    std::array<std::unique_ptr<mce::Mesh>, static_cast<std::size_t>(RenderBucket::Count)> meshes;
    // Immutable pre-upload source for the Blend bucket. The engine consumes
    // Mesh::mMeshData while creating GPU buffers, so the renderer must never
    // use the uploaded Mesh as the source for a later camera sort.
    std::unique_ptr<mce::MeshData> blendSource;
    std::unique_ptr<MixedGlassLiquidMesh> mixedGlassLiquid;
};

struct ProjectionAnimatedBubbleCell {
    std::size_t index{};
    BlockPos local{};
    bool dragDown{};
};

// Temporary (one activation only) GPU owner transfer for unchanged sections.
// This never crosses world/dimension, projection pose or resource-pack reload.
struct RegionMeshRetention {
    std::shared_ptr<structure::LoadedStructure const> source;
    std::map<SubChunkKey,std::size_t> sectionMap;
    std::vector<std::vector<std::size_t>> sectionBlockIndices;
    std::vector<std::set<SubChunkKey>> sectionExtraBlocks;
    std::vector<SectionState> sections;
    // Region transfer owns the paged correction store without flattening
    // millions of cells into a second transient array.
    CorrectionStateStore corrections;
    std::vector<std::uint8_t> missingLayers;
    std::vector<std::uint8_t> actorRendererAvailable;
    std::vector<std::unique_ptr<mce::Mesh>> warningFill, correctionOutline,
        wrongFill, wrongOutline, nativeLiquid, liquidProxy, blockEntityPlaceholder;
    std::vector<std::unique_ptr<PraxisCompatLiquidSectionData>> praxisLiquid;
    std::vector<std::size_t> nativeLiquidCounts, liquidProxyCounts;
    BlockPos anchor{};
    int rotation{},mirror{}, offsetX{},offsetY{},offsetZ{};
    bool countExtras{};
    std::optional<structure::LayerDisplayMode> layerMode;
    std::optional<structure::LayerAxis> layerAxis;
    int displayLayer{};
    float opacity{}, fillOpacity{}, outlineOpacity{},comparisonStrength{},outlineWidth{};
};

struct ProjectionState {
    std::size_t mixedGlassLiquidCursor{};
    std::unique_ptr<RegionMeshRetention> pendingRegionReuse;
    // R11 animated schematic particles. Only the render callback reads/writes
    // this bounded cache, never the async section-build worker. All positions
    // stay projection-local; no real-world particle engine is touched.
    std::vector<ProjectionAnimatedBubbleCell> animatedBubbleCells;
    structure::LoadedStructure const* animatedBubbleSource{};
    std::uint64_t animatedBubbleGeneration{};
    std::size_t animatedBubbleScanCursor{};
    int animatedBubbleMirror{-1};
    int animatedBubbleRotation{-1};
    std::size_t                     blendSortCursor{};
    // CPU mesh-source diagnostic totals are invariant between mesh uploads.
    // Do not rescan every section on every transparent render pass.
    std::uint64_t                   blendSourceScanActivation{};
    std::uint64_t                   blendSourceScanMeshRevision{};
    std::size_t                     blendSourceScanSectionCount{};
    std::size_t                     blendSourceCount{};
    std::size_t                     blendSourceVertices{};
    bool                            blendSourceScanValid{};
    // CPU-only list of sections with native liquid GPU resources or Exact
    // Replay streams. Rebuilt on mesh/activation change, not per frame.
    std::vector<std::size_t>        nativeLiquidCandidateSections;
    std::uint64_t                   nativeLiquidCandidateActivation{};
    std::uint64_t                   nativeLiquidCandidateMeshRevision{};
    std::size_t                     nativeLiquidCandidateSectionCount{};
    bool                            nativeLiquidCandidateCacheValid{};
    // From MarmieQi 26.51.4: cache draw slots independently of heavy per-frame
    // geometry and use view-prioritized, starvation-free section scheduling.
    ProjectionDrawCache drawCache;
    ProjectionTransparentBatchCache transparentDrawBatches;
    std::uint64_t meshUploadRevision{};
    std::uint64_t visibilityRevision{};
    // Bounded mesh residency: sections outside the retention ring are
    // released a few per frame; renewed requests keep the scan cursor so
    // continuous camera movement cannot starve a large blueprint (23b259d).
    ProjectionResidencyScan                  residencyScan;
    std::uint64_t meshBuildSelections{};
    std::uint64_t visibilityBoundsGeneration{};
    std::size_t visibilityBoundsSections{};
    int visibilityBoundsMirror{-999};
    int visibilityBoundsRotation{-999};

    bool                            enabled{};
    bool                            placementCoordinatesInvalid{};
    BlockPos                        anchor{};
    IClientInstance*                client{};
    Level*                          level{};
    Dimension*                      dimension{};
    int                             dimensionId{};
    std::optional<mce::TexturePtr>  terrainTexture;
    std::optional<TextureVariant>   terrainTextureVariant;
    // R5 experimental per-projection material variant. This is a separately
    // owned copy of the vanilla blend material with depth-write disabled;
    // never mutate the shared ItemInHandRenderer material.
    std::unique_ptr<mce::MaterialPtr> glassPrivateBlendMaterial;
    std::weak_ptr<mce::RenderMaterialInfo> glassPrivateBlendBase;
    std::shared_ptr<structure::LoadedStructure const> structure;
    std::uint64_t                   structureGeneration{};
    std::uint64_t                   activationGeneration{};
    std::unique_ptr<BlockTessellator> blockTessellator;
    CorrectionStateStore correctionStates;
    std::optional<CorrectionStateStore::Snapshot> correctionSnapshot;
    // Both builders share this read boundary. Only the active state is mutable;
    // a worker retains a bounded, immutable neighborhood instead of all cells.
    CorrectionState renderCorrectionState(std::size_t index) const {
        return correctionSnapshot ? correctionSnapshot->renderState(index) : correctionStates.renderState(index);
    }
    // Component absence from the same bounded correction read; overall
    // CorrectionState remains the authoritative progress/Verifier result.
    std::vector<std::uint8_t>       missingLayers;
    std::optional<SectionMissingLayerSnapshot> sectionMissingLayerSnapshot;
    bool hasBuildMissingLayers(std::size_t index) const {
        return sectionMissingLayerSnapshot ? sectionMissingLayerSnapshot->find(index) != nullptr
            : index < missingLayers.size();
    }
    std::uint8_t buildMissingLayers(std::size_t index) const {
        if (!sectionMissingLayerSnapshot) return missingLayers.at(index);
        auto const* value = sectionMissingLayerSnapshot->find(index);
        if (!value) throw std::out_of_range("section missing-layer snapshot missing block");
        return *value;
    }
    bool buildBodyShouldRender(std::size_t index) const {
        // The model stream must honor the same layer visibility mask as
        // correction outlines. buildCorrectionState() reads the raw truth
        // (including hidden cells); renderCorrectionState() maps hidden
        // cells to Correct in both synchronous and worker snapshots.
        return projectionBodyShouldRender(renderCorrectionState(index), buildMissingLayers(index));
    }
    bool buildLiquidShouldRender(std::size_t index) const {
        return projectionLiquidShouldRender(renderCorrectionState(index), buildMissingLayers(index));
    }
    // One byte per structure block. This is updated by the existing bounded
    // correction scan, so the HUD never performs its own world-block queries.
    std::vector<uchar>              progressCorrect;
    // Bit 0: real body has expected item/block type (may have wrong states);
    // bit 1: liquid layer has expected type. Completeness still uses
    // progressCorrect. This separate mask is only for material quantity.
    std::vector<uchar>              progressMaterialTypePresent;
    // Changes whenever the per-cell correctness snapshot changes. Consumers
    // can use this to rebuild derived data (such as material HUD rows) only
    // when necessary instead of rescanning the structure every frame.
    std::uint64_t                   progressRevision{};
    // 0 = no placement error, 1 = wrong block type, 2 = wrong state/direction.
    // Updating one byte and two counters keeps the HUD O(1) per frame.
    std::vector<uchar>              progressErrorKind;
    std::vector<uchar>              blockActorRendererAvailable;
    std::optional<SectionBlockSnapshot> sectionBlockSnapshot;
    bool hasBuildBlockState(std::size_t index) const {
        return sectionBlockSnapshot ? sectionBlockSnapshot->find(index) != nullptr
            : index < correctionStates.size() && index < blockActorRendererAvailable.size();
    }
    CorrectionState buildCorrectionState(std::size_t index) const {
        if (!sectionBlockSnapshot) return correctionStates.get(index);
        auto const* value = sectionBlockSnapshot->find(index);
        if (!value) throw std::out_of_range("section correction snapshot missing block");
        return static_cast<CorrectionState>(value->correction);
    }
    bool buildActorRendererAvailable(std::size_t index) const {
        if (!sectionBlockSnapshot) return blockActorRendererAvailable.at(index) != 0;
        auto const* value = sectionBlockSnapshot->find(index);
        if (!value) throw std::out_of_range("section actor snapshot missing block");
        return value->actorRenderer != 0;
    }
    std::uint64_t                   progressCorrectCount{};
    std::uint64_t                   progressVisibleCorrectCount{};
    std::uint64_t                   progressWrongTypeCount{};
    std::uint64_t                   progressWrongStateCount{};
    std::uint64_t                   progressExtraCount{};
    CorrectionScanScheduler         correctionScan;
    CorrectionScanProgress          initialCorrectionScan;
    CorrectionAirScan               extraScan;
    std::vector<CorrectionSectionIndex> correctionIndex;
    CorrectionEventQueue correctionEvents;
    CorrectionSubChunkQueue correctionSubChunks;
    CorrectionWorkCursor correctionWorkCursor;
    std::uint64_t correctionFrames{}, correctionMicros{}, correctionPeakMicros{}, correctionWork{};
    std::uint64_t correctionEmptyFastCells{}, correctionEmptyProbes{};
    std::uint64_t correctionReceivedCells{}, correctionReceivedSections{};
    std::array<std::uint64_t, 4> correctionWorkByKind{};
    std::vector<BrokenProjectionCell> pendingBrokenCells;
    bool                            cachedCountExtras{true};
    int                             cachedRotation{-1};
    int                             cachedMirror{-1};
    int                             cachedOffsetX{};
    int                             cachedOffsetY{};
    int                             cachedOffsetZ{};
    std::optional<structure::LayerDisplayMode> cachedLayerDisplayMode;
    int                             cachedDisplayLayer{-1};
    std::optional<structure::LayerAxis> cachedLayerAxis;
    float                           cachedOpacity{-1.0f};
    float                           cachedCorrectionFillOpacity{-1.0f};
    float                           cachedCorrectionOutlineOpacity{-1.0f};
    float                           cachedComparisonStrength{-1.0f};
    float                           cachedCorrectionOutlineWidth{-1.0f};
    std::vector<std::vector<std::size_t>> sectionBlockIndices;
    // Extra blocks occupy cells that have no render-block index. The detected
    // set covers the visible source region for HUD counting; the render set and
    // per-section sets contain only the current visible range. All stay sparse.
    std::map<SubChunkKey, std::size_t>    localSectionIndices;
    std::vector<SubChunkKey>              localSectionKeys;
    std::set<SubChunkKey>                 detectedExtraBlockPositions;
    std::unordered_map<SubChunkKey, std::size_t, SubChunkKeyHash> detectedExtraSectionCounts;
    std::set<SubChunkKey>                 extraBlockPositions;
    std::vector<std::set<SubChunkKey>>    sectionExtraBlockPositions;
    // Correction meshes are split by category so the see-through (X-ray) option
    // can apply to the wrong-type/wrong-state markers only, never to the many
    // "missing" outlines. warningFill/correctionOutline hold the MISSING cells;
    // wrongFill/wrongOutline hold WrongType + WrongState.
    std::vector<std::unique_ptr<mce::Mesh>> warningFillSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> correctionOutlineSectionMeshes;
    ProjectionCorrectionBatchCache           correctionDrawBatches;
    std::vector<std::unique_ptr<mce::Mesh>> wrongFillSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> wrongOutlineSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> nativeLiquidSectionMeshes;
    std::vector<std::unique_ptr<PraxisCompatLiquidSectionData>> praxisCompatLiquidSections;
    std::unique_ptr<PraxisCompatLiquidSectionData> praxisCompatLiquidAggregate;
    std::vector<std::size_t>                       praxisCompatLiquidAggregateOrder;
    LiquidBoundaryMaskCache                       praxisCompatLiquidBoundaryMaskCache;
    bool                                           praxisCompatLiquidAggregateDirty{true};
    std::vector<std::unique_ptr<mce::Mesh>> liquidProxySectionMeshes;
    std::vector<std::size_t>                nativeLiquidSectionCellCounts;
    std::vector<std::size_t>                liquidProxySectionCellCounts;
    std::vector<std::unique_ptr<mce::Mesh>> blockEntityPlaceholderSectionMeshes;
    std::unique_ptr<mce::Mesh>              structureBoundsMesh;
    // Released together with dimension/world meshes by the existing owner.
    std::unique_ptr<overlay::BoundsWireframe> verifierTargetBounds;
    std::unique_ptr<overlay::BoundsWireframe> overallPlacementBounds;
    std::unique_ptr<overlay::BoundsWireframe> regionSelectionBounds;
    // Reuse one tiny GPU wireframe per native source subregion. Only bounds
    // changes recreate meshes; toggling visible blocks does not rebuild them.
    std::vector<std::unique_ptr<overlay::BoundsWireframe>> nativeRegionBounds;
    std::vector<SectionState>               sections;
    // Preserve the indexed section ownership used by lifecycle, placement,
    // and block-entity render culling. CorrectionStateStore uses a separate
    // compact index, but existing consumers still require this mapping.
    std::vector<std::size_t>                blockToSection;
    std::size_t                             dirtySectionCursor{};
    std::uint64_t                           meshWorkerGeneration{};
    int                                     consecutiveMeshWorkerFailures{};
    bool                                    asyncMeshBuildingEnabled{true};
    std::uint64_t                           meshWorkerUploadedSections{};
    std::uint64_t                           meshWorkerCorrectionOnlySections{};
    std::uint64_t                           meshWorkerSnapshotMicros{};
    std::uint64_t                           meshWorkerSnapshotDataMicros{};
    std::uint64_t                           meshWorkerChunkViewMicros{};
    std::uint64_t                           meshWorkerBuildMicros{};
    std::uint64_t                           meshWorkerUploadMicros{};
    std::uint64_t                           meshWorkerPeakSnapshotMicros{};
    std::uint64_t                           meshWorkerPeakSnapshotDataMicros{};
    std::uint64_t                           meshWorkerPeakChunkViewMicros{};
    std::uint64_t                           meshWorkerPeakBuildMicros{};
    // Per-worker component timings survive only in an immutable worker result.
    std::uint64_t                           meshBodyClassifyMicros{};
    std::uint64_t                           meshBodyTessellateMicros{};
    std::uint64_t                           meshLiquidMicros{};
    std::uint64_t                           meshAuxMicros{};
    std::uint64_t                           meshPlaceholderMicros{};
    std::uint64_t                           meshCorrectionSetupMicros{};
    std::uint64_t                           meshCorrectionOutlineMicros{};
    std::uint64_t                           meshCorrectionFillMicros{};
    std::uint64_t                           meshOutlineAttemptedEdges{};
    std::uint64_t                           meshOutlineEmittedEdges{};
    std::uint64_t                           meshOutlineGridBytes{};
    std::uint64_t                           meshFillCandidateFaces{};
    std::uint64_t                           meshFillEmittedFaces{};
    std::uint64_t                           meshFillRunOriginalFaces{};
    std::uint64_t                           meshFillRunQuads{};
    // Accumulated on render thread for the existing 64-section perf summary.
    std::uint64_t                           meshWorkerClassifyTotalMicros{};
    std::uint64_t                           meshWorkerTessellateTotalMicros{};
    std::uint64_t                           meshWorkerLiquidTotalMicros{};
    std::uint64_t                           meshWorkerAuxTotalMicros{};
    std::uint64_t                           meshWorkerPlaceholderTotalMicros{};
    std::uint64_t                           meshWorkerCorrectionSetupTotalMicros{};
    std::uint64_t                           meshWorkerCorrectionOutlineTotalMicros{};
    std::uint64_t                           meshWorkerCorrectionFillTotalMicros{};
    std::uint64_t                           meshWorkerOutlineAttemptedEdges{};
    std::uint64_t                           meshWorkerOutlineEmittedEdges{};
    std::uint64_t                           meshWorkerOutlineGridBytes{};
    std::uint64_t                           meshWorkerFillCandidateFaces{};
    std::uint64_t                           meshWorkerFillEmittedFaces{};
    std::uint64_t                           meshWorkerFillRunOriginalFaces{};
    std::uint64_t                           meshWorkerFillRunQuads{};
    std::uint64_t                           meshWorkerPeakUploadMicros{};
    // One immutable placement view shared by the active projection and
    // workers. Body and liquid layers resolve independently through the same
    // on-demand coordinate inversion, preserving waterlogged two-layer cells
    // (fork semantics previously provided by the separate liquid map).
    std::shared_ptr<ProjectionWorldLookup const> worldLookup{
        std::make_shared<ProjectionWorldLookup>()
    };
    std::shared_ptr<ExpectedBlockActorMap>   expectedWorldBlockActors{
        std::make_shared<ExpectedBlockActorMap>()
    };
    std::vector<ProjectedBlockActor>         projectedBlockActors;

    // Building the transformed virtual-world lookup is O(N) in structure
    // cells. Keep it incremental so a large projection never monopolizes one
    // render frame before the async mesh worker can start.
    bool                     placementBuildActive{};
    std::size_t              placementBuildCursor{};
    std::vector<Vec3>        placementCenterSums;
    std::vector<std::size_t> placementCenterCounts;

    NativeLiquidTelemetry                   nativeLiquidTelemetry;
    bool                                    meshPreflightDone{};
    MeshDiagnosticGate                      meshDiagnosticGate;

    // Bounded stationary-glass diagnostics. These counters are sampled by
    // the existing render callback and reset only when the R3 summary is
    // emitted; they never drive rendering decisions.
    std::uint64_t glassR3LastDiagnosticMs{};
    std::uint64_t glassR3RenderCallbacks{};
    std::uint64_t glassR3InvalidCameraCallbacks{};
    std::uint64_t glassR3CameraChangedSamples{};
    std::uint64_t glassR3CameraJitterSamples{};
    std::uint64_t glassR3RetainedDrawCalls{};
    std::uint64_t glassR3BlendDrawCalls{};
    std::uint64_t glassR3RevisionChurn{};
    std::uint64_t glassR3UnsupportedLayoutObservations{};
    std::uint64_t glassR3SortInvalid{};
    std::uint64_t glassR3SortChanged{};
    std::optional<Vec3> glassR3LastCamera;

    // R4 bounded source-cache/sort diagnostics. These are reset when the
    // periodic R4_BUILT line is emitted; they never drive rendering.
    std::uint64_t glassR4LastDiagnosticMs{};
    std::uint64_t glassR4SortAttempts{};
    std::uint64_t glassR4SortReplacements{};
    std::uint64_t glassR4SortIdentityRestores{};
    std::uint64_t glassR4SortUploadFailures{};
    std::uint64_t glassR4SortSourceUnavailable{};
    std::uint64_t glassR4SortInvalid{};
};

} // namespace lholo::projection::detail
