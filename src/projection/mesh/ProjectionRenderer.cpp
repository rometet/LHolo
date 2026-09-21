// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/mesh/ProjectionRenderer.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionLiquidCompatColor.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionVirtualWorld.h"
#include "plugin/LHolo.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

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
#include "mc/deps/core/math/Vec2.h"
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
std::atomic_bool gPraxisCompatBuildLogged{};

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

void submitPraxisCompatLiquidImmediately(
    ScreenContext&                       screenContext,
    PraxisCompatLiquidSectionData const& data,
    mce::MaterialPtr const&              material,
    TextureVariant const&                terrainTexture
) {
    Tessellator& tessellator = screenContext.tessellator;
    tessellator.begin(
        Tessellator::DebugContextCallback{},
        mce::PrimitiveMode::QuadList,
        static_cast<int>(data.positions.size()),
        true
    );
    for (std::size_t vertex = 0; vertex < data.positions.size(); ++vertex) {
        auto const rgba = unpackAbgr(data.derivedColors[vertex]);
        tessellator.color(
            static_cast<float>(rgba.red) / 255.0F,
            static_cast<float>(rgba.green) / 255.0F,
            static_cast<float>(rgba.blue) / 255.0F,
            static_cast<float>(rgba.alpha) / 255.0F
        );
        auto const& uv = data.uv0[vertex];
        tessellator.tex2(Vec2{uv.x, uv.y});
        auto const& position = data.positions[vertex];
        tessellator.vertex(position.x, position.y, position.z);
    }
    MeshHelpers::renderMeshImmediately(
        screenContext,
        tessellator,
        material,
        terrainTexture,
        SupplementaryFieldAutoGenerationMode{1},
        emptyOffscreenCaptureDescription()
    );
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

    // The default 26.51 candidate replays the separately generated Praxis
    // true/false stream through ScreenContext's own Tessellator and the typed
    // MeshHelpers immediate entry. The Phase 3C retained mesh remains present
    // and is used only by the diagnostic switch or an explicit fail-closed
    // section fallback.
    std::vector<std::size_t> nativeLiquidSections;
    bool nativeLiquidDrawnWithSignText{};
    if (renderAlphaLayer) {
        for (std::size_t section = 0;
             section < state.nativeLiquidSectionMeshes.size();
             ++section) {
            auto const& mesh = state.nativeLiquidSectionMeshes[section];
            auto const& compat = state.praxisCompatLiquidSections[section];
            if ((mesh && mesh->isValid()) || (compat && compat->ready())) {
                nativeLiquidSections.push_back(section);
            }
        }
        std::sort(
            nativeLiquidSections.begin(),
            nativeLiquidSections.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                return distanceSquared(worldCenter(lhs)) > distanceSquared(worldCenter(rhs));
            }
        );
        if (!nativeLiquidSections.empty()) {
            if (auto const* signText = render::resolveSignTextMaterial()) {
                state.nativeLiquidTelemetry.nativeLiquidSignTextResolved = 1;
                state.nativeLiquidTelemetry.praxisCompatSignTextResolved = 1;
                if (!gSignTextResolvedLogged.exchange(true, std::memory_order_acq_rel)) {
                    logger().info("NATIVE_LIQUID_SIGN_TEXT_RESOLVED material=sign_text");
                }
                state.nativeLiquidTelemetry.praxisCompatTerrainTextureReady =
                    state.terrainTextureVariant ? 1U : 0U;
                auto const usePraxisCompat =
                    ActiveNativeLiquidRenderPath == NativeLiquidRenderPath::PraxisCompat
                    && state.terrainTextureVariant.has_value();
                for (auto const section : nativeLiquidSections) {
                    auto const& compat = state.praxisCompatLiquidSections[section];
                    if (usePraxisCompat && compat && compat->ready()) {
                        ScopedShaderColorWhite shaderColorWhite{
                            renderContext.mScreenContext
                        };
                        state.nativeLiquidTelemetry.praxisCompatShaderColorWhite = 1;
                        submitPraxisCompatLiquidImmediately(
                            renderContext.mScreenContext,
                            *compat,
                            *signText,
                            *state.terrainTextureVariant
                        );
                        ++state.nativeLiquidTelemetry.praxisCompatImmediateSubmits;
                        if (!gPraxisCompatBuildLogged.exchange(
                                true,
                                std::memory_order_acq_rel
                            )) {
                            auto const& telemetry = state.nativeLiquidTelemetry;
                            logger().info(
                                "PRAXIS_COMPAT_LIQUID_BUILD generationBeginFlag=1 tessellateFlag=0 layer=3 uvRemapped={} culled={} derivedColors={} shaderColorWhite={} submitPath=MeshHelpers::renderMeshImmediately material=sign_text terrainTextureReady={} immediateSubmits={}",
                                telemetry.praxisCompatUvRemappedVertices,
                                telemetry.praxisCompatVerticesCulled,
                                telemetry.praxisCompatDerivedColorVertices,
                                telemetry.praxisCompatShaderColorWhite,
                                telemetry.praxisCompatTerrainTextureReady,
                                telemetry.praxisCompatImmediateSubmits
                            );
                        }
                        continue;
                    }

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
                    ++state.nativeLiquidTelemetry.nativeLiquidSignTextDraws;
                    if (ActiveNativeLiquidRenderPath == NativeLiquidRenderPath::PraxisCompat) {
                        ++state.nativeLiquidTelemetry.praxisCompatRetainedFallbackDraws;
                    }
                }
                nativeLiquidDrawnWithSignText = true;
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
    auto sortBackToFront = [&](std::vector<VisibleMesh>& meshes) {
        std::sort(meshes.begin(), meshes.end(), [&](VisibleMesh const& lhs, VisibleMesh const& rhs) {
            return distanceSquared(worldCenter(lhs.section))
                > distanceSquared(worldCenter(rhs.section));
        });
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
    auto collectBucket = [&](std::size_t bucket) {
        std::vector<VisibleMesh> result;
        result.reserve(state.sections.size());
        for (std::size_t section = 0; section < state.sections.size(); ++section) {
            auto const& mesh = state.sections[section].meshes[bucket];
            if (mesh && mesh->isValid()) {
                result.push_back({bucket, section});
            }
        }
        return result;
    };

    auto const opaqueBucket = static_cast<std::size_t>(RenderBucket::Opaque);
    auto const alphaBucket = static_cast<std::size_t>(RenderBucket::Alpha);
    auto const alphaOneSidedBucket = static_cast<std::size_t>(RenderBucket::AlphaOneSided);
    auto const blendBucket = static_cast<std::size_t>(RenderBucket::Blend);
    if (structureOpacity >= 0.999f) {
        auto opaqueMeshes = collectBucket(opaqueBucket);
        auto alphaMeshes = collectBucket(alphaBucket);
        auto alphaOneSidedMeshes = collectBucket(alphaOneSidedBucket);
        auto transparentMeshes = collectBucket(blendBucket);
        sortBackToFront(transparentMeshes);

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
            renderMeshes(transparentMeshes, blendMaterial);
        }
    } else if (renderAlphaLayer) {
        // True projection transparency needs a blending material even for
        // normally opaque/cutout blocks. Sort every bucket together.
        std::vector<VisibleMesh> transparentMeshes;
        for (std::size_t bucket = 0;
             bucket < static_cast<std::size_t>(RenderBucket::Count);
             ++bucket) {
            auto bucketMeshes = collectBucket(bucket);
            transparentMeshes.insert(
                transparentMeshes.end(), bucketMeshes.begin(), bucketMeshes.end()
            );
        }
        sortBackToFront(transparentMeshes);
        renderMeshes(transparentMeshes, blendMaterial);
    }

    // A missing runtime sign_text material is an explicit diagnostic fallback.
    // Geometry stays visible for comparison, but telemetry makes it impossible
    // to mistake the legacy material for a successful Phase 3A candidate.
    if (renderAlphaLayer && !nativeLiquidDrawnWithSignText) {
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
        std::vector<std::size_t> liquidSections;
        for (std::size_t liquidSection = 0;
             liquidSection < state.liquidProxySectionMeshes.size();
             ++liquidSection) {
            auto const& mesh = state.liquidProxySectionMeshes[liquidSection];
            if (mesh && mesh->isValid()) liquidSections.push_back(liquidSection);
        }
        std::sort(
            liquidSections.begin(),
            liquidSections.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                return distanceSquared(worldCenter(lhs)) > distanceSquared(worldCenter(rhs));
            }
        );
        for (auto const liquidSection : liquidSections) {
            auto& mesh = *state.liquidProxySectionMeshes[liquidSection];
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
