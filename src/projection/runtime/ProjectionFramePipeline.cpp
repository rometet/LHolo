// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionFramePipeline.h"

#include "projection/correction/ProjectionCorrectionTracker.h"
#include "projection/correction/CorrectionUpdate.h"
#include "projection/mesh/ProjectionMeshScheduler.h"
#include "projection/mesh/ProjectionMeshUpload.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "projection/core/ProjectionState.h"
#include "structure/StructureLoader.h"

#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {

void processProjectionOpaqueFrame(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockSource&                          region,
    Vec3 const&                           cameraPosition,
    LegacyStructureSettings const&        transformSettings,
    ProjectionSectionBuildSettings const& buildSettings,
    structure::LayerDisplayMode           layerDisplayMode,
    int                                   displayLayer,
    structure::LayerAxis                  layerAxis
) {
    if (consumeMeshWorkerFatalFailure()) {
        state.asyncMeshBuildingEnabled = false;
        disableMeshWorkerForSession();
        stopMeshWorker();
        for (auto& section : state.sections) {
            if (!section.buildInFlight) continue;
            section.buildInFlight = false;
            section.dirty = true;
        }
        LHolo::getInstance().getSelf().getLogger().error(
            "Projection mesh task/result publication failed; retrying affected sections through synchronous fallback"
        );
    }
    auto& blockTessellator = *state.blockTessellator;
    blockTessellator.setRegion(region);

    auto const correctionChanges = runCorrectionUpdate(
        [&] {
            return updateCorrectionTracker(
                state,
                region,
                transformSettings,
                buildSettings.mirrorMode,
                buildSettings.rotationTurns,
                buildSettings.offsetX,
                buildSettings.offsetY,
                buildSettings.offsetZ,
                layerDisplayMode,
                displayLayer,
                layerAxis
            );
        },
        []() noexcept { markProjectionWorldEventsFailed(); }
    );
    if (correctionChanges.overall) {
        publishPlacedProgress(state.progressCorrectCount);
    }
    if (correctionChanges.visible) {
        publishVisiblePlacedProgress(state.progressVisibleCorrectCount);
    }
    if (correctionChanges.errors) {
        publishErrorProgress(
            state.progressWrongTypeCount,
            state.progressWrongStateCount,
            state.progressExtraCount
        );
    }

    // Consume completed CPU data only in the opaque render pass.
    uploadCompletedProjectionMeshes(state, tessellator);
    if (state.asyncMeshBuildingEnabled) {
        // Bounds use the same exact extent/white UVs as synchronous fallback.
        // Generate their 24 vertices once on the GPU owner, before CPU tasks.
        buildStructureBoundsMesh(state, tessellator, Tessellator::UploadMode::Buffered, buildSettings);
    }
    scheduleProjectionMeshBuild(
        state, tessellator, region, cameraPosition, buildSettings
    );
    buildNextProjectionSectionSynchronously(
        state, tessellator, blockTessellator, region, buildSettings
    );
}

} // namespace lholo::projection::detail
