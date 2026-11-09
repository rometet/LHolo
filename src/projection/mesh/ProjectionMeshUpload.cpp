// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/mesh/NativeMeshDataClone.h"
#include "projection/mesh/ProjectionMeshUpload.h"

#include "plugin/LHolo.h"
#include "projection/core/ProjectionInternalTypes.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "projection/core/ProjectionState.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "mc/client/renderer/Tessellator.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "ll/api/mod/NativeMod.h"

namespace lholo::projection::detail {
namespace {

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

void markSectionDirty(ProjectionState& state, std::size_t section, bool incremental) {
    if (section >= state.sections.size()) return;
    auto& sectionState = state.sections[section];
    sectionState.dirty = true;
    sectionState.modelDirty = true;
    sectionState.incrementalDirty = sectionState.incrementalDirty || incremental;
    ++sectionState.requestedRevision;
}

void mergeNativeLiquidTelemetry(
    NativeLiquidTelemetry&       destination,
    NativeLiquidTelemetry const& source
) {
    destination.nativeLiquidCellsAttempted += source.nativeLiquidCellsAttempted;
    destination.nativeLiquidTessellationPositive += source.nativeLiquidTessellationPositive;
    destination.nativeLiquidTessellationZero += source.nativeLiquidTessellationZero;
    destination.nativeLiquidTessellationFailure += source.nativeLiquidTessellationFailure;
    destination.nativeLiquidVertices += source.nativeLiquidVertices;
    destination.nativeLiquidUvVertices += source.nativeLiquidUvVertices;
    destination.nativeLiquidUvAtlasResolvedCells
        += source.nativeLiquidUvAtlasResolvedCells;
    destination.nativeLiquidUvRemappedVertices
        += source.nativeLiquidUvRemappedVertices;
    destination.nativeLiquidUvRemapFailures += source.nativeLiquidUvRemapFailures;
    destination.nativeLiquidVerticesBeforeCull
        += source.nativeLiquidVerticesBeforeCull;
    destination.nativeLiquidVerticesCulled
        += source.nativeLiquidVerticesCulled;
    destination.nativeLiquidVerticesAfterCull
        += source.nativeLiquidVerticesAfterCull;
    destination.nativeLiquidFacePairsCulled
        += source.nativeLiquidFacePairsCulled;
    destination.nativeLiquidCullSkipped += source.nativeLiquidCullSkipped;
    destination.praxisCompatCellsAttempted += source.praxisCompatCellsAttempted;
    destination.praxisCompatTessellationPositive
        += source.praxisCompatTessellationPositive;
    destination.praxisCompatTessellationZero += source.praxisCompatTessellationZero;
    destination.praxisCompatTessellationFailure
        += source.praxisCompatTessellationFailure;
    destination.praxisCompatVertices += source.praxisCompatVertices;
    destination.praxisCompatUvRemappedVertices
        += source.praxisCompatUvRemappedVertices;
    destination.praxisCompatUvRemapFailures += source.praxisCompatUvRemapFailures;
    destination.praxisCompatVerticesBeforeCull
        += source.praxisCompatVerticesBeforeCull;
    destination.praxisCompatVerticesCulled
        += source.praxisCompatVerticesCulled;
    destination.praxisCompatVerticesAfterCull
        += source.praxisCompatVerticesAfterCull;
    destination.praxisCompatFacePairsCulled
        += source.praxisCompatFacePairsCulled;
    destination.praxisCompatCullSkipped += source.praxisCompatCullSkipped;
    destination.praxisCompatDerivedColorVertices
        += source.praxisCompatDerivedColorVertices;
    destination.praxisCompatWaterSeedVertices
        += source.praxisCompatWaterSeedVertices;
    destination.praxisCompatLavaNativeVertices
        += source.praxisCompatLavaNativeVertices;
    destination.praxisCompatBuildSections += source.praxisCompatBuildSections;
    destination.praxisCompatCapturedPositions += source.praxisCompatCapturedPositions;
    destination.praxisCompatCapturedNormals += source.praxisCompatCapturedNormals;
    destination.praxisCompatCapturedTangents += source.praxisCompatCapturedTangents;
    destination.praxisCompatCapturedColors += source.praxisCompatCapturedColors;
    destination.praxisCompatCapturedBoneIds += source.praxisCompatCapturedBoneIds;
    destination.praxisCompatCapturedUv0 += source.praxisCompatCapturedUv0;
    destination.praxisCompatCapturedUv1 += source.praxisCompatCapturedUv1;
    destination.praxisCompatCapturedUv2 += source.praxisCompatCapturedUv2;
    destination.praxisCompatCapturedPbrTextureIndices
        += source.praxisCompatCapturedPbrTextureIndices;
    destination.praxisCompatCapturedMers += source.praxisCompatCapturedMers;
    destination.praxisCompatCapturedGeoType += source.praxisCompatCapturedGeoType;
    destination.praxisCompatCapturedQuadInfo += source.praxisCompatCapturedQuadInfo;
    destination.praxisCompatDoubleLiquidBuildSections
        += source.praxisCompatDoubleLiquidBuildSections;
    destination.compositeBodyLiquidCells
        += source.compositeBodyLiquidCells;
    destination.compositeBodyTessellationPositive
        += source.compositeBodyTessellationPositive;
    destination.compositeBodyTessellationZero
        += source.compositeBodyTessellationZero;
    destination.compositeBodyVertices
        += source.compositeBodyVertices;
    destination.nativeLiquidColorVertices += source.nativeLiquidColorVertices;
    destination.nativeLiquidAlphaModifiedVertices
        += source.nativeLiquidAlphaModifiedVertices;
    destination.virtualLiquidQueryHits += source.virtualLiquidQueryHits;
    destination.liquidProxyFallbackCells += source.liquidProxyFallbackCells;
    for (std::size_t layer = 0; layer < destination.nativeLiquidLayerAttempts.size(); ++layer) {
        destination.nativeLiquidLayerAttempts[layer]
            += source.nativeLiquidLayerAttempts[layer];
    }
}

} // namespace

void uploadCompletedProjectionMeshes(ProjectionState& state, Tessellator& tessellator) {
    for(auto const& cancelled:takeCancelledSectionBuilds())
        (void)recoverCancelledSection(cancelled,state.meshWorkerGeneration,state.structureGeneration,state.sections);
    if (auto bufferService = tessellator.mBufferResourceService.lock()) {
        auto const uploadStarted = std::chrono::steady_clock::now();
        for (std::size_t uploaded = 0; uploaded < 2; ++uploaded) {
            if (std::chrono::steady_clock::now() - uploadStarted >= std::chrono::milliseconds(1)) break;
            auto completed = takeCompletedSectionBuilds(1);
            if (completed.empty()) break;
            auto result = std::move(completed.front());
            if (result.workerGeneration != state.meshWorkerGeneration
                || result.structureGeneration != state.structureGeneration
                || result.section >= state.sections.size()) {
                continue;
            }
    
            auto const section = result.section;
            auto const storageReady =
                section < state.warningFillSectionMeshes.size()
                && section < state.correctionOutlineSectionMeshes.size()
                && section < state.wrongFillSectionMeshes.size()
                && section < state.wrongOutlineSectionMeshes.size()
                && section < state.nativeLiquidSectionMeshes.size()
                && section < state.praxisCompatLiquidSections.size()
                && section < state.liquidProxySectionMeshes.size()
                && section < state.nativeLiquidSectionCellCounts.size()
                && section < state.liquidProxySectionCellCounts.size()
                && section < state.blockEntityPlaceholderSectionMeshes.size();
            if (!storageReady) {
                // Fail closed instead of indexing an out-of-sync per-section
                // storage vector. Extra correction sections must extend every
                // parallel section store together.
                state.sections[section].buildInFlight = false;
                state.sections[section].dirty = true;
                logger().error(
                    "Projection section storage mismatch at section {}: sections={} praxis={} nativeLiquid={} liquidProxy={} placeholders={}",
                    section,
                    state.sections.size(),
                    state.praxisCompatLiquidSections.size(),
                    state.nativeLiquidSectionMeshes.size(),
                    state.liquidProxySectionMeshes.size(),
                    state.blockEntityPlaceholderSectionMeshes.size()
                );
                continue;
            }
            state.sections[section].buildInFlight = false;
            if (!result.success) {
                markSectionDirty(state, section, true);
                logger().warn(
                    "Projection mesh worker section {} revision {} failed: {} (expected {}, actual {}; snapshot {} us, build {} us)",
                    section,
                    result.revision,
                    result.failureReason.empty() ? "unknown failure" : result.failureReason,
                    result.expectedVertexCount,
                    result.actualVertexCount,
                    result.snapshotPrepareMicros,
                    result.workerBuildMicros
                );
                if (++state.consecutiveMeshWorkerFailures >= 3) {
                    state.asyncMeshBuildingEnabled = false;
                    disableMeshWorkerForSession();
                    stopMeshWorker();
                    logger().warn("Projection mesh worker failed three times; using synchronous fallback for this session");
                }
                continue;
            }
            if (result.revision != state.sections[section].requestedRevision) {
                state.sections[section].dirty = true;
                continue;
            }
    
            auto const sectionUploadStarted = std::chrono::steady_clock::now();
            auto uploadCpuMesh = [&](std::unique_ptr<mce::Mesh> cpuMesh,
                                     std::string_view name,
                                     std::unique_ptr<mce::MeshData>* originalSource = nullptr) {
                if (!cpuMesh || cpuMesh->mMeshData.get().size() == 0) {
                    return std::unique_ptr<mce::Mesh>{};
                }
                // Never let the engine's upload consume the only CPU source
                // used by the later transparent sorter.
                if (originalSource && !*originalSource) {
                    *originalSource = cloneNativeMeshData(
                        cpuMesh->mMeshData.get()
                    );
                }
                auto data = std::move(cpuMesh->mMeshData.get());
                return std::make_unique<mce::Mesh>(bufferService, std::move(data), false, name);
            };
            try {
                constexpr std::array<std::string_view, static_cast<std::size_t>(RenderBucket::Count)>
                    bucketNames{"LHoloOpaque", "LHoloAlphaTest", "LHoloBlend", "LHoloBlendToOpaque"};
                std::array<std::unique_ptr<mce::Mesh>, static_cast<std::size_t>(RenderBucket::Count)>
                    uploadedMeshes;
                if (!result.correctionOnly) {
                    for (std::size_t bucket = 0; bucket < uploadedMeshes.size(); ++bucket) {
                        uploadedMeshes[bucket] = uploadCpuMesh(
                            std::move(result.sectionMeshes[bucket]),
                            bucketNames[bucket],
                            bucket == static_cast<std::size_t>(RenderBucket::Blend)
                                ? &result.blendSource : nullptr
                        );
                    }
                }
                auto const blendBucket = static_cast<std::size_t>(RenderBucket::Blend);
                if (uploadedMeshes[blendBucket]) {
                    if (!result.blendSource) {
                        throw std::runtime_error("blend original source was unavailable");
                    }
                    if (!uploadedMeshes[blendBucket]->isValid()) {
                        throw std::runtime_error("blend upload produced an invalid mesh");
                    }
                }
                // Snapshot CPU source before upload, publish it only after all
                // section GPU meshes have been successfully installed.
                auto captureCorrectionCpu = [](std::unique_ptr<mce::Mesh> const& mesh)
                    -> std::unique_ptr<mce::MeshData> {
                    if (!mesh || mesh->mMeshData.get().size() == 0) return nullptr;
                    return cloneNativeMeshData(mesh->mMeshData.get());
                };
                auto missingFillSource = captureCorrectionCpu(result.warningFillMesh);
                auto missingOutlineSource = captureCorrectionCpu(result.correctionOutlineMesh);
                auto warningFill = uploadCpuMesh(std::move(result.warningFillMesh), "LHoloWarningFill");
                auto correctionOutline = uploadCpuMesh(
                    std::move(result.correctionOutlineMesh), "LHoloCorrectionOutline"
                );
                auto wrongFill = uploadCpuMesh(std::move(result.wrongFillMesh), "LHoloWrongFill");
                auto wrongOutline = uploadCpuMesh(
                    std::move(result.wrongOutlineMesh), "LHoloWrongOutline"
                );
                auto nativeLiquid = uploadCpuMesh(
                    std::move(result.nativeLiquidMesh), "LHoloNativeLiquid"
                );
                auto liquidProxy = uploadCpuMesh(std::move(result.liquidProxyMesh), "LHoloLiquidProxy");
                auto blockEntityPlaceholder = uploadCpuMesh(
                    std::move(result.blockEntityPlaceholderMesh), "LHoloBlockEntityPlaceholder"
                );
    
                if (!result.correctionOnly) {
                    for (std::size_t bucket = 0; bucket < uploadedMeshes.size(); ++bucket) {
                        state.sections[section].meshes[bucket] = std::move(uploadedMeshes[bucket]);
                    }
                    state.sections[section].blendSource = std::move(result.blendSource);
                    state.sections[section].blendSortKeyValid = false;
                    state.sections[section].blendSortUnsupported = false;
                    state.sections[section].blendSortOrder.clear();
                    ++state.sections[section].blendSortUploadGeneration;
                    // Capture both old and new native-liquid ownership before moving.
                    bool const liquidSourcesChanged =
                        state.nativeLiquidSectionMeshes[section] != nullptr
                        || state.praxisCompatLiquidSections[section] != nullptr
                        || nativeLiquid != nullptr
                        || result.praxisCompatLiquidData != nullptr;
                    state.nativeLiquidSectionMeshes[section] = std::move(nativeLiquid);
                    state.praxisCompatLiquidSections[section] = std::move(
                        result.praxisCompatLiquidData
                    );
                    // Opaque-only section updates cannot affect the native liquid
                    // aggregate. Retain it rather than copying all water/lava
                    // streams again on every unrelated mesh upload.
                    if (liquidSourcesChanged) {
                        state.praxisCompatLiquidAggregate.reset();
                        state.praxisCompatLiquidAggregateOrder.clear();
                        state.praxisCompatLiquidBoundaryMaskCache.clear();
                        state.praxisCompatLiquidAggregateDirty = true;
                    }
                    state.liquidProxySectionMeshes[section] = std::move(liquidProxy);
                    state.nativeLiquidSectionCellCounts[section] = result.nativeLiquidCellCount;
                    state.liquidProxySectionCellCounts[section] = result.liquidProxyCellCount;
                    mergeNativeLiquidTelemetry(
                        state.nativeLiquidTelemetry,
                        result.nativeLiquidTelemetry
                    );
                    state.blockEntityPlaceholderSectionMeshes[section] = std::move(blockEntityPlaceholder);
                    state.sections[section].modelDirty = false;
                }
                state.warningFillSectionMeshes[section] = std::move(warningFill);
                state.correctionOutlineSectionMeshes[section] = std::move(correctionOutline);
                state.wrongFillSectionMeshes[section] = std::move(wrongFill);
                state.wrongOutlineSectionMeshes[section] = std::move(wrongOutline);
                state.sections[section].uploadedRevision = result.revision;
                state.sections[section].meshBounds = result.meshBounds;
                // Only a successfully uploaded original can contribute to a
                // batch. An invalid GPU mesh must never be resurrected by
                // the retained CPU source in a future group upload.
                if (!state.warningFillSectionMeshes[section]
                    || !state.warningFillSectionMeshes[section]->isValid())
                    missingFillSource.reset();
                if (!state.correctionOutlineSectionMeshes[section]
                    || !state.correctionOutlineSectionMeshes[section]->isValid())
                    missingOutlineSource.reset();
                // If cache bookkeeping fails, originals remain valid, and
                // fail-closed batching is simply disabled for the session.
                try {
                    state.correctionDrawBatches.noteUploaded(
                        section, state.sections.size(), ProjectionCorrectionBatchCache::MissingFill,
                        std::move(missingFillSource));
                    state.correctionDrawBatches.noteUploaded(
                        section, state.sections.size(), ProjectionCorrectionBatchCache::MissingOutline,
                        std::move(missingOutlineSource));
                } catch (...) {
                    state.correctionDrawBatches.clear();
                }
                ++state.meshUploadRevision;
                state.drawCache.noteMeshUpload(section);
                state.sections[section].incrementalDirty = false;
                state.sections[section].interactiveDirty = false;
                state.sections[section].dirty = false;
                state.consecutiveMeshWorkerFailures = 0;
                state.meshPreflightDone = false;
                auto const uploadMicros = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - sectionUploadStarted
                    ).count()
                );
                ++state.meshWorkerUploadedSections;
                if (result.correctionOnly)
                    ++state.meshWorkerCorrectionOnlySections;
                state.meshWorkerSnapshotMicros += result.snapshotPrepareMicros;
                state.meshWorkerSnapshotDataMicros += result.snapshotDataMicros;
                state.meshWorkerChunkViewMicros += result.chunkViewMicros;
                state.meshWorkerBuildMicros += result.workerBuildMicros;
                state.meshWorkerClassifyTotalMicros += result.bodyClassifyMicros;
                state.meshWorkerTessellateTotalMicros += result.bodyTessellateMicros;
                state.meshWorkerLiquidTotalMicros += result.liquidMicros;
                state.meshWorkerAuxTotalMicros += result.auxMicros;
                state.meshWorkerPlaceholderTotalMicros += result.placeholderMicros;
                state.meshWorkerCorrectionSetupTotalMicros += result.correctionSetupMicros;
                state.meshWorkerCorrectionOutlineTotalMicros += result.correctionOutlineMicros;
                state.meshWorkerCorrectionFillTotalMicros += result.correctionFillMicros;
                state.meshWorkerOutlineAttemptedEdges += result.outlineAttemptedEdges;
                state.meshWorkerOutlineEmittedEdges += result.outlineEmittedEdges;
                state.meshWorkerOutlineGridBytes += result.outlineGridBytes;
                state.meshWorkerFillCandidateFaces += result.fillCandidateFaces;
                state.meshWorkerFillEmittedFaces += result.fillEmittedFaces;
                state.meshWorkerFillRunOriginalFaces += result.fillRunOriginalFaces;
                state.meshWorkerFillRunQuads += result.fillRunQuads;
                state.meshWorkerUploadMicros += uploadMicros;
                state.meshWorkerPeakSnapshotMicros = std::max(
                    state.meshWorkerPeakSnapshotMicros, result.snapshotPrepareMicros
                );
                state.meshWorkerPeakSnapshotDataMicros = std::max(
                    state.meshWorkerPeakSnapshotDataMicros, result.snapshotDataMicros
                );
                state.meshWorkerPeakChunkViewMicros = std::max(
                    state.meshWorkerPeakChunkViewMicros, result.chunkViewMicros
                );
                state.meshWorkerPeakBuildMicros = std::max(
                    state.meshWorkerPeakBuildMicros, result.workerBuildMicros
                );
                state.meshWorkerPeakUploadMicros = std::max(
                    state.meshWorkerPeakUploadMicros, uploadMicros
                );
                if (state.meshWorkerUploadedSections % 64 == 0) {
                    auto const count = state.meshWorkerUploadedSections;
                    // Six INFO records for a 393-section projection; unlike
                    // DEBUG this survives the default production log level.
                    logger().info(
                        "Projection mesh worker: {} sections; snapshot {}/{} us (data {}/{}, chunkView {}/{}), build {}/{} us, upload {}/{} us; correction snapshot {} B, COW total {} B, overlay-only {}",
                        count,
                        state.meshWorkerSnapshotMicros / count,
                        state.meshWorkerPeakSnapshotMicros,
                        state.meshWorkerSnapshotDataMicros / count,
                        state.meshWorkerPeakSnapshotDataMicros,
                        state.meshWorkerChunkViewMicros / count,
                        state.meshWorkerPeakChunkViewMicros,
                        state.meshWorkerBuildMicros / count,
                        state.meshWorkerPeakBuildMicros,
                        state.meshWorkerUploadMicros / count,
                        state.meshWorkerPeakUploadMicros,
                        result.correctionSnapshotBytes,
                        state.correctionStates.copiedBytes(),
                        state.meshWorkerCorrectionOnlySections
                    );
                    logger().info(
                        "LHolo mesh stages: {} sections avgUs classification={} nativeBody={} liquid={} auxiliary={}",
                        count,
                        state.meshWorkerClassifyTotalMicros / count,
                        state.meshWorkerTessellateTotalMicros / count,
                        state.meshWorkerLiquidTotalMicros / count,
                        state.meshWorkerAuxTotalMicros / count
                    );
                    logger().info(
                        "LHolo correction stages: {} sections avgUs placeholders={} setup={} outlines={} fills={}",
                        count,
                        state.meshWorkerPlaceholderTotalMicros / count,
                        state.meshWorkerCorrectionSetupTotalMicros / count,
                        state.meshWorkerCorrectionOutlineTotalMicros / count,
                        state.meshWorkerCorrectionFillTotalMicros / count
                    );
                    logger().info(
                        "LHolo opaque outline dedup: {} sections inputEdges={} emittedEdges={} gridAvgBytes={}",
                        count,
                        state.meshWorkerOutlineAttemptedEdges,
                        state.meshWorkerOutlineEmittedEdges,
                        state.meshWorkerOutlineGridBytes / count
                    );
                    logger().info(
                        "LHolo fill faces: {} sections candidateFaces={} visibleFaces={}",
                        count,
                        state.meshWorkerFillCandidateFaces,
                        state.meshWorkerFillEmittedFaces
                    );
                    logger().info(
                        "LHolo fill run merges: {} sections originalFaces={} emittedQuads={} savedQuads={} mode=rectangle2D",
                        count,
                        state.meshWorkerFillRunOriginalFaces,
                        state.meshWorkerFillRunQuads,
                        state.meshWorkerFillRunOriginalFaces - state.meshWorkerFillRunQuads
                    );
                }
            } catch (std::exception const& exception) {
                // Restore retry eligibility before any allocating diagnostic.
                markSectionDirty(state, section, true);
                if (++state.consecutiveMeshWorkerFailures >= 3) {
                    state.asyncMeshBuildingEnabled = false;
                    disableMeshWorkerForSession();
                    stopMeshWorker();
                    logger().warn("Projection mesh upload failed three times; using synchronous fallback for this session");
                }
                logger().warn(
                    "Projection mesh upload for section {} revision {} failed: {}",
                    section, result.revision, exception.what()
                );
            } catch (...) {
                markSectionDirty(state, section, true);
                if (++state.consecutiveMeshWorkerFailures >= 3) {
                    state.asyncMeshBuildingEnabled = false;
                    disableMeshWorkerForSession();
                    stopMeshWorker();
                    logger().warn("Projection mesh upload failed three times; using synchronous fallback for this session");
                }
                logger().warn(
                    "Projection mesh upload for section {} revision {} failed with a non-standard exception",
                    section, result.revision
                );
            }
        }
    }
}

} // namespace lholo::projection::detail
