// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionFramePipeline.h"
#include "resourcepacks/WorldPackReloadState.h"

#include "projection/correction/ProjectionCorrectionTracker.h"
#include "projection/correction/CorrectionUpdate.h"
#include "projection/correction/CorrectionFrameBudget.h"
#include "projection/correction/CorrectionScanScheduler.h"
#include "projection/correction/CorrectionWorkQueues.h"
#include "projection/mesh/ProjectionMeshScheduler.h"
#include "projection/mesh/ProjectionMeshUpload.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/Logger.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "projection/core/ProjectionState.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>

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

    CorrectionFrameBudget budget;
    auto started = CorrectionFrameBudget::Clock::now();
    // Intake is bounded too. Leave most of the frame slice for actual
    // comparisons when world notifications arrive continuously.
    for (int batch = 0; batch < CorrectionFrameBudget::MaximumIntakeBatches && budget.available(); ++batch) {
        if (batch && CorrectionFrameBudget::Clock::now() - started >= CorrectionFrameBudget::IntakeDuration)
            break;
        auto events = takePendingBlockChangeSections(CorrectionFrameBudget::EventSectionsPerBatch);
        auto chunks = batch < CorrectionFrameBudget::LoadedIntakeBatches
            ? takePendingLoadedSubChunks(CorrectionFrameBudget::LoadedSectionsPerBatch) : std::vector<SubChunkKey>{};
        if (events.empty() && chunks.empty())
            break;
        state.correctionReceivedSections += events.size();
        for (auto const& event : events) state.correctionReceivedCells += event.size();
        // The single-file fork has no ProjectionWorldBounds helper; keep the
        // same half-open structure AABB test inline. Structure cells occupy
        // [origin, origin + size) per axis after the build transform.
        bool const rotated = (buildSettings.rotationTurns & 1) != 0;
        std::array<long long, 3> const origin{
            static_cast<long long>(state.anchor.x) + buildSettings.offsetX,
            static_cast<long long>(state.anchor.y) + buildSettings.offsetY,
            static_cast<long long>(state.anchor.z) + buildSettings.offsetZ,
        };
        std::array<long long, 3> const structureMax{
            origin[0] + (rotated ? state.structure->sizeZ : state.structure->sizeX),
            origin[1] + state.structure->sizeY,
            origin[2] + (rotated ? state.structure->sizeX : state.structure->sizeZ),
        };
        for (auto& event : events)
            state.correctionEvents.push(std::move(event).clipped(origin, structureMax));
        for (auto const& chunk : chunks) {
            auto [x, y, z] = chunk;
            std::array<long long, 3> const lo{
                static_cast<long long>(x) * 16,
                static_cast<long long>(y) * 16,
                static_cast<long long>(z) * 16,
            };
            bool const overlaps = lo[0] < structureMax[0] && lo[0] + 16 > origin[0]
                && lo[1] < structureMax[1] && lo[1] + 16 > origin[1]
                && lo[2] < structureMax[2] && lo[2] + 16 > origin[2];
            if (overlaps)
                state.correctionSubChunks.push({x, y, z});
        }
    }
    auto distance = [&](CorrectionPosition const& position, float scale) {
        auto dx = position[0] * scale - cameraPosition.x;
        auto dy = position[1] * scale - cameraPosition.y;
        auto dz = position[2] * scale - cameraPosition.z;
        return dx * dx + dy * dy + dz * dz;
    };
    while (budget.available()) {
        auto selected = selectCorrectionWork(
            1, state.correctionWorkCursor,
            [&](std::size_t, CorrectionWorkKind kind)
                -> std::optional<CorrectionPriority> {
                if (kind == CorrectionWorkKind::Event) {
                    if (auto event = state.correctionEvents.front())
                        return CorrectionPriority{0, distance(event->position, 1)};
                } else if (kind == CorrectionWorkKind::Loaded) {
                    if (auto chunk = state.correctionSubChunks.front())
                        return CorrectionPriority{0, distance(chunk->position, 16)};
                } else if (kind == CorrectionWorkKind::Initial) {
                    if (!state.correctionScan.empty())
                        return CorrectionPriority{0, 0.0f};
                } else if (!state.extraScan.complete()) {
                    return CorrectionPriority{0, 0.0f};
                }
                return std::nullopt;
            }
        );
        if (!selected)
            break;
        auto workBefore = budget.work();
        auto changes = runCorrectionUpdate(
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
                    layerAxis,
                    cameraPosition,
                    selected->kind,
                    budget
                );
            },
            []() noexcept { markProjectionWorldEventsFailed(); }
        );
        state.correctionWorkByKind[static_cast<std::size_t>(selected->kind)] += budget.work() - workBefore;
        if (changes.overall) {
            publishPlacedProgress(state.progressCorrectCount);
        }
        if (changes.visible) {
            publishVisiblePlacedProgress(state.progressVisibleCorrectCount);
        }
        if (changes.errors) {
            publishErrorProgress(
                state.progressWrongTypeCount,
                state.progressWrongStateCount,
                state.progressExtraCount
            );
        }
    }
    auto const micros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            CorrectionFrameBudget::Clock::now() - started
        ).count()
    );
    ++state.correctionFrames;
    state.correctionMicros += micros;
    state.correctionWork += budget.work();
    state.correctionPeakMicros = std::max(state.correctionPeakMicros, micros);
    if (state.correctionFrames % 120 == 0) {
        auto const emptyCells = state.correctionEmptyFastCells;
        auto const probes = state.correctionEmptyProbes;
        state.correctionEmptyFastCells = state.correctionEmptyProbes = 0;
        auto& logger = LHolo::getInstance().getSelf().getLogger();
        bool const report = logger.shouldLog(ll::io::LogLevel::Debug);
        std::size_t pendingCells{}, pendingSections{}, pendingLoaded{}, blocked{}, dirty{};
        if (report) {
            // Disabled diagnostics must not scan every section or acquire the
            // incoming world-event mutex just to discard the formatted result.
            pendingCells = state.correctionEvents.size();
            pendingSections = state.correctionEvents.sections();
            pendingLoaded = state.correctionSubChunks.size();
            for (std::size_t index = 0; index < state.sections.size(); ++index) {
                if (!state.sections[index].dirty) continue;
                ++dirty;
                blocked += projectionSectionHasPendingCorrection(state, index, buildSettings);
            }
        }
        if (report) {
            auto incoming = pendingProjectionEventCounts();
            logger.debug(
                "Projection correction queues: incoming {} cells / {} tiles / {} loaded; pending {} cells / {} tiles / {} loaded; intake {} cells / {} tiles; work event/loaded/initial/extra {}/{}/{}/{}; dirty {} (coalescing {})",
                incoming.cells, incoming.sections, incoming.loadedSections, pendingCells, pendingSections, pendingLoaded,
                state.correctionReceivedCells, state.correctionReceivedSections, state.correctionWorkByKind[0],
                state.correctionWorkByKind[1], state.correctionWorkByKind[2], state.correctionWorkByKind[3], dirty, blocked);
            logger.debug(
                "Projection correction: average/peak {} / {} us, average {} work units, target {} us; empty fast-path {} cells / {} probes",
                state.correctionMicros / state.correctionFrames,
                state.correctionPeakMicros,
                state.correctionWork / state.correctionFrames,
                CorrectionFrameBudget::DefaultDuration.count(),
                emptyCells,
                probes
            );
        }
        state.correctionReceivedCells = state.correctionReceivedSections = 0;
        state.correctionWorkByKind = {};
        state.correctionFrames = state.correctionMicros = 0;
        state.correctionPeakMicros = state.correctionWork = 0;
    }

    // Consume completed CPU data only in the opaque render pass.
    // Buffered uploads use the normal engine resource service registered at
    // the render owner. This marker is not a private GPU completion receipt.
    resourcepacks::state().submitted(resourcepacks::Domain::Projection);
    uploadCompletedProjectionMeshes(state, tessellator);
    scheduleProjectionMeshBuild(
        state, tessellator, region, cameraPosition, buildSettings
    );
    buildNextProjectionSectionSynchronously(
        state, tessellator, blockTessellator, region, buildSettings
    );
}

} // namespace lholo::projection::detail
