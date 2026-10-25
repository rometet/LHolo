#include "render/RenderCameraPosition.h"
#include "overlay/BoundsWireframe.h"
#include "resourcepacks/WorldPackReloadState.h"

#include "render/OverlayMaterials.h"
#include "render/BoxWireframeMesh.h"
#include "render/ScopedShaderColor.h"

#include <cmath>
#include <cstdint>

#include <Windows.h>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/renderer/Camera.h"

namespace lholo::overlay {

namespace {

Vec3 renderCameraPosition(BaseActorRenderContext const& renderContext) {
    auto const camera = render::readRenderCameraPosition(renderContext.mImpl.get());
    return camera ? Vec3{camera->x, camera->y, camera->z} : Vec3{};
}

OffscreenCaptureDescription const& emptyOffscreenCaptureDescription() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}



} // namespace

BoundsWireframe::~BoundsWireframe() = default;

void BoundsWireframe::setBounds(BlockPos const& min, BlockPos const& max, std::uint32_t color) {
    // Uniform-color materials permit color edits without rebuilding the GPU mesh.
    mColor = color;
    if (mHasBounds && mMin == min && mMax == max) return;
    mMin = min;
    mMax = max;
    mHasBounds = true;
    mMesh.reset();
}

void BoundsWireframe::clear() {
    mHasBounds = false;
    mMesh.reset();
}

void BoundsWireframe::render(BaseActorRenderContext& renderContext, bool renderAlphaLayer) {
    if (!mHasBounds) return;

    if (!renderAlphaLayer && !mMesh) {
        // Inclusive block positions map to exact grid-cell boundaries.
        float const x1 = static_cast<float>(
            static_cast<std::int64_t>(mMax.x)-mMin.x+1);
        float const y1 = static_cast<float>(
            static_cast<std::int64_t>(mMax.y)-mMin.y+1);
        float const z1 = static_cast<float>(
            static_cast<std::int64_t>(mMax.z)-mMin.z+1);
        resourcepacks::state().submitted(resourcepacks::Domain::Projection);
        mMesh=render::buildBoxWireframeMesh(
            renderContext.mScreenContext.tessellator,
            {0,0,0},{x1,y1,z1},"LHoloSelectionBounds");
        return;
    }

    if (!renderAlphaLayer || !mMesh || !mMesh->isValid()) return;
    auto& client = renderContext.mClientInstance;
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return;
    // Shared native selection-outline material. Its color is a shader uniform,
    // not a vertex color, and is restored before Minecraft's next draw.
    auto const* nativeOutline=render::selectionOutlineMaterial(levelRenderer);
    if (!nativeOutline || !render::materialExists(*nativeOutline)) return;
    auto const& material=*nativeOutline;
    auto const texture = render::resolveWhiteTextureVariant(levelRenderer);

    Vec3 const camera = renderCameraPosition(renderContext);
    auto matrix = renderContext.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(
        static_cast<float>(mMin.x) - camera.x,
        static_cast<float>(mMin.y) - camera.y,
        static_cast<float>(mMin.z) - camera.z
    );
    render::ScopedShaderColor boxColor{renderContext.mScreenContext.currentShaderColor,mColor};
    resourcepacks::state().submitted(resourcepacks::Domain::Projection);
    mMesh->renderMesh(
        renderContext.mScreenContext,
        material,
        texture,
        0,
        mMesh->mVertexCount.get().value_or(0u),
        emptyOffscreenCaptureDescription(),
        nullptr
    );
}

} // namespace lholo::overlay
