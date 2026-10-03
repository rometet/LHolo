#include "render/RenderCameraPosition.h"
// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Projection frame orchestration: structure activation, world-context check,
// opaque/transparent mesh submission, hit-select suppression and the
// post-block-entity frame entry. Session state is accessed through the
// ProjectionSession contract.

#include "projection/runtime/ProjectionRenderFrame.h"
#include "projection/runtime/ProjectionSession.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionLiquidCompatColor.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionCoordinateBounds.h"
#include "projection/core/ProjectionState.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "projection/mesh/ProjectionRenderer.h"
#include "projection/mesh/ProjectionSectionBuilder.h"
#include "projection/runtime/ProjectionFramePipeline.h"
#include "projection/runtime/ProjectionInvalidation.h"
#include "projection/runtime/ProjectionLifecycle.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "projection/runtime/VerifierHighlightRules.h"
#include "projection/world/ProjectionPlacement.h"

#include "overlay/BoundsWireframe.h"
#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "structure/capture/StructureCapture.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"
#include "structure/SchematicRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <Windows.h>
#include <exception>
#include <memory>
#include <tuple>
#include <utility>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

#include "ll/api/mod/NativeMod.h"

namespace lholo::projection::detail {
namespace {

Vec3 renderCameraPosition(BaseActorRenderContext const& renderContext) {
    auto const camera = render::readRenderCameraPosition(renderContext.mImpl.get());
    return camera ? Vec3{camera->x, camera->y, camera->z} : Vec3{};
}

void resetWorldAfterExit() {
    place::resetWorldSession();
    structure::capture::clear();
    structure::resetWorldSession();
    structure::showActionHint(
        i18n::Message{i18n::TextKey::ActionHintWorldExited},
        structure::kProjectionLifecycleHintDurationMs
    );
}

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

bool enableStructureProjection(
    ProjectionState& state,
    BaseActorRenderContext& renderContext,
    std::shared_ptr<structure::LoadedStructure const> loaded
) {
    ProjectionState next;
    if (!prepareProjectionState(next, renderContext, std::move(loaded))) return false;
    auto& client = renderContext.mClientInstance;
    auto* player = client.getLocalPlayer();
    // prepareProjectionState() checked the player earlier, but world/dimension
    // transitions may invalidate it before this function continues.
    if (!player) return false;
    if (auto const anchor = ProjectionSession::getInstance().consumeAnchor()) {
        next.anchor = BlockPos{anchor->x, anchor->y, anchor->z};
    } else {
        auto const& position = player->getPosition();
        // Player position is the feet/air cell. Default a newly loaded
        // structure to the supporting ground cell directly below it.
        auto const cell = checkedBlockCell({position.x, position.y, position.z}, 1);
        if (!cell) {
            logger().error("Projection activation rejected an invalid player position");
            structure::showActionHint(i18n::Message{i18n::TextKey::StatusCoordinatesInvalid});
            return false;
        }
        next.anchor = BlockPos{(*cell)[0], (*cell)[1] - 1, (*cell)[2]};
    }
    state = std::move(next);
    state.enabled = true;
    try {
        if (meshWorkerIsDisabledForSession()) {
            state.asyncMeshBuildingEnabled = false;
        } else {
            state.meshWorkerGeneration = startMeshWorker();
        }
    } catch (std::exception const& exception) {
        state.asyncMeshBuildingEnabled = false;
        disableMeshWorkerForSession();
        logger().warn("Projection mesh worker initialization failed; using synchronous fallback: {}", exception.what());
    } catch (...) {
        state.asyncMeshBuildingEnabled = false;
        disableMeshWorkerForSession();
        logger().warn("Projection mesh worker initialization failed; using synchronous fallback");
    }
    auto const transform = structure::detail::StructureSession::getInstance().transform();
    if (auto const origin = checkedProjectionOrigin({state.anchor.x, state.anchor.y, state.anchor.z},
            {transform.offsetX, transform.offsetY, transform.offsetZ},
            {state.structure->sizeX, state.structure->sizeY, state.structure->sizeZ}, transform.rotation)) {
        publishProjectionWorldEventInterest(makeProjectionWorldEventInterest(
            *state.structure, *origin, transform.mirror, transform.rotation
        ));
    } else {
        publishProjectionWorldEventInterest(WorldEventInterest{});
    }
    attachProjectionWorldEvents(player->getLevel(), player->getDimensionBlockSource());
    initializePublishedBuildProgress(state.structure->renderBlocks.size());
    structure::recordProjectionAnchor(state.anchor.x, state.anchor.y, state.anchor.z);
    logger().info(
        "Structure projection enabled: {} renderable blocks at ({}, {}, {})",
        state.structure->renderBlocks.size(),
        state.anchor.x,
        state.anchor.y,
        state.anchor.z
    );
    return true;
}

void suspendProjectionDimension(ProjectionState& state) {
    auto const anchor = state.anchor;
    place::resetDimensionSession();
    structure::resetDimensionSession();
    ProjectionSession::getInstance().suspendForDimension(
        state.structureGeneration,
        state.dimensionId,
        ProjectionAnchor{anchor.x, anchor.y, anchor.z}
    );
    suspendProjectionState(state);
    structure::showActionHint(
        i18n::Message{i18n::TextKey::ActionHintProjectionSuspended},
        structure::kProjectionLifecycleHintDurationMs
    );
}

void renderProjection(
    ProjectionState&          state,
    BaseActorRenderContext&   renderContext,
    bool                      renderAlphaLayer
) {
    auto& client = renderContext.mClientInstance;
    auto* player  = client.getLocalPlayer();

    // The render callback can race a world/dimension transition. Never
    // dereference a player or immutable projection maps after they disappear.
    if (!player || !state.structure || !state.blockTessellator
        || !state.expectedWorldBlocks || !state.expectedWorldLiquids
        || !state.expectedWorldBlockIndices || !state.expectedWorldBlockActors) {
        return;
    }

    auto& tessellator = renderContext.mScreenContext.tessellator;
    Vec3 const camera = renderCameraPosition(renderContext);
    if (!renderAlphaLayer) {
        auto const transform = structure::detail::StructureSession::getInstance().transform();
        auto const mirrorMode = transform.mirror;
        auto const rotationTurns = transform.rotation;
        auto const mirror = getProjectionMirror(mirrorMode);
        auto const rotation = getProjectionRotation(rotationTurns);
        LegacyStructureSettings transformSettings{
            mirror,
            rotation,
            nullptr,
            BoundingBox{}
        };
        bool const identityTransform = mirrorMode == 0 && rotationTurns == 0;
        auto const offsetX = transform.offsetX;
        auto const offsetY = transform.offsetY;
        auto const offsetZ = transform.offsetZ;
        auto const layerDisplayMode = transform.layerDisplayMode;
        auto const layerAxis = transform.layerAxis;
        auto const origin = checkedProjectionOrigin(
            {state.anchor.x, state.anchor.y, state.anchor.z},
            {offsetX, offsetY, offsetZ},
            {state.structure->sizeX, state.structure->sizeY, state.structure->sizeZ}, rotationTurns
        );
        if (!origin) {
            if (!state.placementCoordinatesInvalid) {
                logger().error("Projection origin/extent exceeds the int-coordinate domain; adjust its offset");
                structure::showActionHint(i18n::Message{i18n::TextKey::StatusCoordinatesInvalid});
            }
            state.placementCoordinatesInvalid = true;
            return;
        }
        state.placementCoordinatesInvalid = false;
        auto const maxLayer = layerAxis == structure::LayerAxis::Material
            ? std::max(0, static_cast<int>(state.structure->materialCount) - 1)
            : std::max(
                0,
                (layerAxis == structure::LayerAxis::X ? state.structure->sizeX : structure::layerCount(structure::PlacementTransform{{state.structure->sizeX,state.structure->sizeY,state.structure->sizeZ},{},rotationTurns,mirrorMode}.placedSize(), layerAxis)) - 1
            );
        auto const displayLayer = std::clamp(
            transform.displayLayer, 0, maxLayer
        );
        auto& session = ProjectionSession::getInstance();
        auto const structureOpacity = session.opacity();
        auto const correctionFillOpacity = session.correctionFillOpacity();
        auto const correctionOutlineOpacity = session.correctionOutlineOpacity();
        auto const comparisonStrength = session.comparisonStrength();
        auto const correctionOutlineWidth = session.correctionOutlineWidth();
        auto const invalidation = reconcileProjectionInvalidation(
            state,
            ProjectionInvalidationSettings{
                .mirrorMode               = mirrorMode,
                .rotationTurns            = rotationTurns,
                .offsetX                  = offsetX,
                .offsetY                  = offsetY,
                .offsetZ                  = offsetZ,
                .layerDisplayMode         = layerDisplayMode,
                .displayLayer             = displayLayer,
                .layerAxis                = layerAxis,
                .structureOpacity         = structureOpacity,
                .correctionFillOpacity    = correctionFillOpacity,
                .correctionOutlineOpacity = correctionOutlineOpacity,
                .comparisonStrength       = comparisonStrength,
                .correctionOutlineWidth   = correctionOutlineWidth
            }
        );
        if (invalidation.placementViewChanged() || state.placementBuildActive) {
            auto const placementReady = rebuildProjectionPlacement(
                state,
                player->getDimensionBlockSource(),
                renderContext.mBlockEntityRenderDispatcher,
                transformSettings,
                ProjectionPlacementSettings{
                    .mirrorMode        = mirrorMode,
                    .rotationTurns     = rotationTurns,
                    .offsetX           = offsetX,
                    .offsetY           = offsetY,
                    .offsetZ           = offsetZ,
                    .layerDisplayMode  = layerDisplayMode,
                    .displayLayer      = displayLayer,
                    .layerAxis         = layerAxis,
                    .identityTransform = identityTransform
                },
                invalidation.placementViewChanged()
            );
            if (!placementReady) return;
        }
        ProjectionSectionBuildSettings const sectionBuildSettings{
            .mirror                   = mirror,
            .rotation                 = rotation,
            .mirrorMode               = mirrorMode,
            .rotationTurns            = rotationTurns,
            .offsetX                  = offsetX,
            .offsetY                  = offsetY,
            .offsetZ                  = offsetZ,
            .structureOpacity         = structureOpacity,
            .correctionFillOpacity    = correctionFillOpacity,
            .correctionOutlineOpacity = correctionOutlineOpacity,
            .comparisonStrength       = comparisonStrength,
            .correctionOutlineWidth   = correctionOutlineWidth,
            .identityTransform        = identityTransform
        };
        processProjectionOpaqueFrame(
            state,
            tessellator,
            player->getDimensionBlockSource(),
            camera,
            transformSettings,
            sectionBuildSettings,
            layerDisplayMode,
            displayLayer,
            layerAxis
        );
    }

    // The alpha callback can arrive while the opaque callback is still
    // incrementally constructing the initial placement snapshot. Do not render
    // or schedule against a partial virtual world.
    if (state.placementBuildActive || state.placementCoordinatesInvalid) return;
    // Keep the authoritative update/worker pipeline converging while hidden;
    // visibility gates submission, without retaining a backlog of world facts.
    if (!structure::detail::StructureSession::getInstance().visible()) return;

    // Keep vanilla world queries at their real BlockPos, but do not upload large
    // absolute coordinates to the GPU. Render vertices relative to the projection
    // origin, matching the strategy used by chunk meshes.
    BlockPos const renderOrigin{
        state.anchor.x + state.cachedOffsetX,
        state.anchor.y + state.cachedOffsetY,
        state.anchor.z + state.cachedOffsetZ
    };
    auto const structureOpacity = ProjectionSession::getInstance().opacity();

    submitProjectedBlockActorPass(
        state,
        renderContext,
        player->getDimensionBlockSource(),
        camera,
        renderAlphaLayer
    );

    auto matrix = renderContext.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(
        static_cast<float>(renderOrigin.x) - camera.x,
        static_cast<float>(renderOrigin.y) - camera.y,
        static_cast<float>(renderOrigin.z) - camera.z
    );

    auto& itemRenderer = renderContext.mItemInHandRenderer;
    auto const& blendMaterial = itemRenderer.mMatBlendBlock.get();
    if (blendMaterial.mRenderMaterialInfoPtr.get() == nullptr) return;

    if (!state.terrainTextureVariant) {
        logger().error("Projection terrain texture is not available");
        return;
    }

    if (state.meshDiagnosticGate.inspect(!state.meshPreflightDone, GetTickCount64())) {
        auto const countValid = [](auto const& meshes) {
            return std::count_if(meshes.begin(), meshes.end(), [](auto const& mesh) {
                return mesh && mesh->isValid();
            });
        };
        std::size_t normalMeshes{};
        for (auto const& sectionState : state.sections) {
            for (auto const& mesh : sectionState.meshes) {
                if (mesh && mesh->isValid()) ++normalMeshes;
            }
        }
        auto const warningMeshes = countValid(state.warningFillSectionMeshes);
        auto const outlineMeshes = countValid(state.correctionOutlineSectionMeshes);
        auto const wrongFillMeshes = countValid(state.wrongFillSectionMeshes);
        auto const wrongOutlineMeshes = countValid(state.wrongOutlineSectionMeshes);
        auto const nativeLiquidMeshes = countValid(state.nativeLiquidSectionMeshes);
        auto const praxisCompatLiquidSections = std::count_if(
            state.praxisCompatLiquidSections.begin(),
            state.praxisCompatLiquidSections.end(),
            [](auto const& data) { return data && data->ready(); }
        );
        auto const liquidMeshes = countValid(state.liquidProxySectionMeshes);
        auto const placeholderMeshes = countValid(state.blockEntityPlaceholderSectionMeshes);
        if (normalMeshes + warningMeshes + outlineMeshes + wrongFillMeshes
            + wrongOutlineMeshes + nativeLiquidMeshes + praxisCompatLiquidSections
            + liquidMeshes + placeholderMeshes != 0) {
            state.meshPreflightDone = true;
            auto const& telemetry = state.nativeLiquidTelemetry;
            logger().info(
                "PHASE3C_NATIVE_LIQUID_TELEMETRY attempted={} positive={} zero={} failure={} vertices={} uv0={} uvAtlasResolvedCells={} uvRemappedVertices={} uvRemapFailures={} verticesBeforeCull={} verticesCulled={} verticesAfterCull={} facePairsCulled={} cullSkipped={} colors={} alphaModified={} virtualLiquidHits={} signTextResolved={} signTextDraws={} terrainBlendResolved={} terrainBlendDraws={} legacyMaterialDraws={} proxyFallbackCells={} proxyDrawCells={} nativeMeshes={} proxyMeshes={}",
                telemetry.nativeLiquidCellsAttempted,
                telemetry.nativeLiquidTessellationPositive,
                telemetry.nativeLiquidTessellationZero,
                telemetry.nativeLiquidTessellationFailure,
                telemetry.nativeLiquidVertices,
                telemetry.nativeLiquidUvVertices,
                telemetry.nativeLiquidUvAtlasResolvedCells,
                telemetry.nativeLiquidUvRemappedVertices,
                telemetry.nativeLiquidUvRemapFailures,
                telemetry.nativeLiquidVerticesBeforeCull,
                telemetry.nativeLiquidVerticesCulled,
                telemetry.nativeLiquidVerticesAfterCull,
                telemetry.nativeLiquidFacePairsCulled,
                telemetry.nativeLiquidCullSkipped,
                telemetry.nativeLiquidColorVertices,
                telemetry.nativeLiquidAlphaModifiedVertices,
                telemetry.virtualLiquidQueryHits,
                telemetry.nativeLiquidSignTextResolved,
                telemetry.nativeLiquidSignTextDraws,
                telemetry.nativeLiquidTerrainBlendResolved,
                telemetry.nativeLiquidTerrainBlendDraws,
                telemetry.nativeLiquidLegacyMaterialDraws,
                telemetry.liquidProxyFallbackCells,
                telemetry.liquidProxyDrawCells,
                nativeLiquidMeshes,
                liquidMeshes
            );
            logger().info(
                "PRAXIS_EXACT_REPLAY_TELEMETRY path={} attempted={} positive={} zero={} failure={} vertices={} uvRemapped={} uvFailures={} beforeCull={} culled={} afterCull={} facePairs={} cullSkipped={} derivedColors={} waterSeedVertices={} lavaNativeVertices={} buildSections={} positions={} normals={} tangents={} colors={} boneIds={} uv0={} uv1={} uv2={} pbr={} mers={} geoType={} quadInfo={} fullNativeStreamsPreserved={} textureRefSubmit={} terrainTextureBound={} perVertexReemit={} doubleLiquidBuild={} shaderColorWhite={} signTextResolved={} terrainTextureReady={} immediateSubmits={} submitPerFrame={} verticesReplayedPerFrame={} replayMicros={} submitMicros={} aggregateBuilds={} retainedFallbackDraws={} compatSections={}",
                ActiveNativeLiquidRenderPath == NativeLiquidRenderPath::PraxisCompat
                    ? "PraxisExactReplay" : "LHoloRetained",
                telemetry.praxisCompatCellsAttempted,
                telemetry.praxisCompatTessellationPositive,
                telemetry.praxisCompatTessellationZero,
                telemetry.praxisCompatTessellationFailure,
                telemetry.praxisCompatVertices,
                telemetry.praxisCompatUvRemappedVertices,
                telemetry.praxisCompatUvRemapFailures,
                telemetry.praxisCompatVerticesBeforeCull,
                telemetry.praxisCompatVerticesCulled,
                telemetry.praxisCompatVerticesAfterCull,
                telemetry.praxisCompatFacePairsCulled,
                telemetry.praxisCompatCullSkipped,
                telemetry.praxisCompatDerivedColorVertices,
                telemetry.praxisCompatWaterSeedVertices,
                telemetry.praxisCompatLavaNativeVertices,
                telemetry.praxisCompatBuildSections,
                telemetry.praxisCompatCapturedPositions,
                telemetry.praxisCompatCapturedNormals,
                telemetry.praxisCompatCapturedTangents,
                telemetry.praxisCompatCapturedColors,
                telemetry.praxisCompatCapturedBoneIds,
                telemetry.praxisCompatCapturedUv0,
                telemetry.praxisCompatCapturedUv1,
                telemetry.praxisCompatCapturedUv2,
                telemetry.praxisCompatCapturedPbrTextureIndices,
                telemetry.praxisCompatCapturedMers,
                telemetry.praxisCompatCapturedGeoType,
                telemetry.praxisCompatCapturedQuadInfo,
                telemetry.praxisCompatFullNativeStreamsPreserved,
                telemetry.praxisCompatTextureRefSubmit,
                telemetry.praxisCompatTerrainTextureBound,
                telemetry.praxisCompatPerVertexReemit,
                telemetry.praxisCompatDoubleLiquidBuildSections,
                telemetry.praxisCompatShaderColorWhite,
                telemetry.praxisCompatSignTextResolved,
                telemetry.praxisCompatTerrainTextureReady,
                telemetry.praxisCompatImmediateSubmits,
                telemetry.praxisCompatImmediateSubmitsPerFrame,
                telemetry.praxisCompatVerticesReplayedPerFrame,
                telemetry.praxisCompatReplayMicros,
                telemetry.praxisCompatSubmitMicros,
                telemetry.praxisCompatAggregateBuilds,
                telemetry.praxisCompatRetainedFallbackDraws,
                praxisCompatLiquidSections
            );
            logger().info(
                "PRAXIS_LIQUID_EFFECTIVE_ALPHA waterAlpha={} waterSeedVertices={} lavaNativeVertices={}",
                PraxisWaterDerivedAlpha,
                telemetry.praxisCompatWaterSeedVertices,
                telemetry.praxisCompatLavaNativeVertices
            );
            logger().info(
                "PRAXIS_SUBMERGED_BODY_TELEMETRY compositeCells={} positive={} zero={} vertices={}",
                telemetry.compositeBodyLiquidCells,
                telemetry.compositeBodyTessellationPositive,
                telemetry.compositeBodyTessellationZero,
                telemetry.compositeBodyVertices
            );
        }
    }

    try {
        submitProjectionMeshPass(
            state,
            renderContext,
            client,
            renderOrigin,
            camera,
            structureOpacity,
            renderAlphaLayer,
            ProjectionSession::getInstance().structureBoundsEnabled(),
            ProjectionSession::getInstance().correctionSeeThrough(),
            ProjectionSession::getInstance().missingSeeThrough()
        );
    } catch (std::exception const& exception) {
        logger().error("Projection immediate mesh submission failed: {}", exception.what());
        resetProjectionState(state);
        return;
    } catch (...) {
        logger().error("Projection immediate mesh submission failed with an unknown exception");
        resetProjectionState(state);
        return;
    }
}

} // namespace

bool shouldSuppressProjectionHitSelect(BlockPos const& pos) {
    if (projectionWorldEventsFailed()) return false;
    if (!structure::detail::StructureSession::getInstance().visible()) return false;
    return ProjectionSession::getInstance().withLockedState(
        [&](ProjectionState& state, overlay::BoundsWireframe&) {
            if (state.enabled && state.structure && !state.placementCoordinatesInvalid && !state.placementBuildActive) {
                auto const found = state.expectedWorldBlockIndices->find(
                    std::tuple{pos.x, pos.y, pos.z}
                );
                if (found != state.expectedWorldBlockIndices->end() && found->second < state.correctionStates.size()) {
                    auto const correctionState = state.correctionStates[found->second];
                    if (correctionState == CorrectionState::WrongType
                        || correctionState == CorrectionState::WrongState) {
                        // LHolo already renders a complete red/yellow hull and
                        // outline for this cell. Vanilla's coincident hit-select
                        // overlay adds a second surface only while the crosshair
                        // targets it, producing the observed flicker.
                        return true;
                    }
                }
                auto const x = static_cast<std::int64_t>(pos.x) - state.anchor.x - state.cachedOffsetX;
                auto const y = static_cast<std::int64_t>(pos.y) - state.anchor.y - state.cachedOffsetY;
                auto const z = static_cast<std::int64_t>(pos.z) - state.anchor.z - state.cachedOffsetZ;
                auto const sizeX = state.cachedRotation & 1 ? state.structure->sizeZ : state.structure->sizeX;
                auto const sizeZ = state.cachedRotation & 1 ? state.structure->sizeX : state.structure->sizeZ;
                if (x < 0 || x >= sizeX || y < 0 || y >= state.structure->sizeY || z < 0 || z >= sizeZ) return false;
                BlockPos const transformed{static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)};
                auto const local = inverseTransformStructurePosition(
                    transformed,
                    *state.structure,
                    state.cachedMirror,
                    state.cachedRotation
                );
                if (state.extraBlockPositions.contains(
                        std::tuple{local.x, local.y, local.z}
                    )) {
                    // Extra cells use the same correction material as the
                    // red/yellow markers, so the vanilla coincident selection
                    // overlay must be suppressed for the same reason.
                    return true;
                }
            }
            return false;
        }
    );
}

void renderProjectionFrame(BaseActorRenderContext& renderContext, bool renderAlphaLayer) {
    bool clearStructure = false;
    bool worldExitPending = consumeWorldExitRequest();
    if (worldExitPending) {
        resetWorldAfterExit();
        return;
    }
    ProjectionSession::getInstance().withLockedState(
        [&](ProjectionState& state, overlay::BoundsWireframe& captureBounds) {
            // The Level listener already joins mesh workers before destruction.
            // In-progress render calls need the same lifetime barrier: merely
            // checking an exit flag before a frame leaves a teardown/use gap.
            std::lock_guard lifecycleLock(projectionWorldLifecycleMutex());
            if (consumeWorldExitRequest()) {
                worldExitPending = true;
                return;
            }
            if (projectionDimensionSourceDestroyed() && state.enabled) {
                suspendProjectionDimension(state);
                return;
            }
            if (auto const bounds = structure::capture::getBounds()) {
                captureBounds.setBounds(
                    BlockPos{bounds->min.x, bounds->min.y, bounds->min.z},
                    BlockPos{bounds->max.x, bounds->max.y, bounds->max.z},
                    0xFF0000FF
                );
            } else {
                captureBounds.clear();
            }
            captureBounds.render(renderContext, renderAlphaLayer);

            if (consumeWorldExitRequest()) {
                worldExitPending = true;
                return;
            }

            if (auto loaded = structure::getLoaded(); loaded && loaded->generation != state.structureGeneration) {
                auto& client = renderContext.mClientInstance;
                auto* player = client.getLocalPlayer();
                if (!player) return;
                auto& session = ProjectionSession::getInstance();
                auto const activationStatus = session.prepareDimensionActivation(
                    loaded->generation,
                    static_cast<int>(player->getDimensionId())
                );
                if (activationStatus == DimensionActivationStatus::Deferred) {
                    return;
                }
                resetProjectionState(state);
                bool activated = false;
                {
                    // Level destruction and structure activation must not pass
                    // each other between the exit check and the first use of
                    // the new state's Level/Dimension pointers.
                    std::lock_guard lifecycleLock(projectionWorldLifecycleMutex());
                    if (consumeWorldExitRequest()) {
                        worldExitPending = true;
                    } else {
                        activated = enableStructureProjection(
                            state,
                            renderContext,
                            std::move(loaded)
                        );
                    }
                }
                if (worldExitPending) return;
                if (!activated) {
                    resetProjectionState(state);
                    logger().error("Could not enable loaded structure projection");
                } else {
                    session.cancelDimensionSuspension();
                    if (activationStatus == DimensionActivationStatus::Resuming) {
                        structure::showActionHint(
                            i18n::Message{i18n::TextKey::ActionHintProjectionRestored},
                            structure::kProjectionLifecycleHintDurationMs
                        );
                    }
                }
            }

            if (!state.enabled) return;
            if (projectionWorldEventsFailed()) {
                structure::detail::StructureSession::getInstance().setStatus(
                    i18n::Message{i18n::TextKey::StatusWorldEventsFailed});
                return;
            }
            auto& client = renderContext.mClientInstance;
            auto const contextStatus = classifyProjectionContext(
                state,
                client,
                client.getLocalPlayer()
            );
            if (contextStatus == ProjectionContextStatus::Unavailable) return;
            if (contextStatus == ProjectionContextStatus::WorldChanged) {
                resetProjectionState(state);
                clearStructure = true;
                return;
            }
            if (contextStatus == ProjectionContextStatus::DimensionChanged) {
                suspendProjectionDimension(state);
                return;
            }
            // The selected report row is a value snapshot. Validate it against
            // the active renderer's world, structure and full placement before
            // reusing the existing single-cell wireframe rendering path.
            std::optional<std::array<int,3>> highlighted;
            auto const view=structure::capture::getClientViewSnapshot();
            auto const loaded=structure::getLoaded();
            if(view && loaded && loaded==state.structure && loaded->generation==state.structureGeneration) {
                auto const transform=structure::detail::StructureSession::getInstance().transform();
                if(auto const target=structure::schematic::highlightTarget(
                    view->worldEpoch,state.dimensionId,state.structureGeneration)) {
                    highlighted=verifierHighlightPosition(*target,{
                        view->worldEpoch,state.dimensionId,state.structureGeneration,
                        {static_cast<std::int64_t>(state.anchor.x)+transform.offsetX,
                         static_cast<std::int64_t>(state.anchor.y)+transform.offsetY,
                         static_cast<std::int64_t>(state.anchor.z)+transform.offsetZ},
                        transform.rotation,transform.mirror,transform.layerAxis,
                        transform.layerDisplayMode,transform.displayLayer,transform.visible,transform.countExtras});
                }
            }
            if(highlighted) {
                if(!state.verifierTargetBounds)state.verifierTargetBounds=std::make_unique<overlay::BoundsWireframe>();
                BlockPos const position{(*highlighted)[0],(*highlighted)[1],(*highlighted)[2]};
                state.verifierTargetBounds->setBounds(position,position,0xFF33FFFFU);
                state.verifierTargetBounds->render(renderContext,renderAlphaLayer);
            } else if(state.verifierTargetBounds) {
                state.verifierTargetBounds->clear();
            }
            renderProjection(state, renderContext, renderAlphaLayer);
        }
    );
    if (worldExitPending) {
        resetWorldAfterExit();
        return;
    }
    if (clearStructure) structure::clear();
}

} // namespace lholo::projection::detail
