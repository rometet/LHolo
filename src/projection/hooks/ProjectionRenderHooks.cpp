// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/hooks/ProjectionRenderHooks.h"

#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "app/ScopeExit.h"
#include "overlay/ImGuiOverlay.h"
#include "projection/runtime/ProjectionRenderFrame.h"
#include "projection/core/ProjectedPistonAppearance.h"
#include "projection/mesh/ProjectedPistonRenderScope.h"
#include "render/OverlayMaterials.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"

#include <atomic>

#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/model/models/PistonArmModel.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/deps/renderer/ShaderColor.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "ll/api/memory/Hook.h"

namespace lholo::projection::detail {
namespace {

std::atomic_bool gProjectedPistonAppearanceLogged{};

LL_TYPE_INSTANCE_HOOK(
    ProjectedPistonArmRenderHook,
    ll::memory::HookPriority::Normal,
    PistonArmModel,
    &PistonArmModel::renderArm,
    void,
    ScreenContext& screenContext,
    float progress
) {
    app::hook_lifecycle::DetourGuard guard;
    auto const* projected = activeProjectedPistonRender;
    if (!guard || !projected || projected->model != this
        || !projected->blendMaterial
        || !render::materialExists(*projected->blendMaterial)) {
        origin(screenContext, progress);
        return;
    }
    // ModelPart rendering can use the model's skinning variants instead of
    // mDefaultMaterial. Derive both variants through the engine constructor,
    // and restore their shared owners along with the default material.
    MaterialVariants ghostVariants{*projected->blendMaterial};
    auto& variants = mMaterialVariants.get();
    auto& skinning = variants.mSkinningMaterialPtr->mRenderMaterialInfoPtr;
    auto& skinningColor = variants.mSkinningColorMaterialPtr->mRenderMaterialInfoPtr;
    auto const savedSkinning = skinning;
    auto const savedSkinningColor = skinningColor;
    app::ScopeExit restoreVariants{[&]() noexcept {
        skinning = savedSkinning;
        skinningColor = savedSkinningColor;
    }};
    skinning = ghostVariants.mSkinningMaterialPtr->mRenderMaterialInfoPtr;
    skinningColor = ghostVariants.mSkinningColorMaterialPtr->mRenderMaterialInfoPtr;
    auto& shader = screenContext.currentShaderColor;
    ScopedPistonAppearance appearance{
        mDefaultMaterial->mRenderMaterialInfoPtr,
        projected->blendMaterial->mRenderMaterialInfoPtr,
        shader.color.get(), shader.dirty, projected->opacity
    };
    if (!gProjectedPistonAppearanceLogged.exchange(true, std::memory_order_relaxed)) {
        auto const& info = projected->blendMaterial->mRenderMaterialInfoPtr;
        auto const* material = info ? info->mPtr.get() : nullptr;
        LHolo::getInstance().getSelf().getLogger().info(
            "PISTON_PROJECTION_APPEARANCE opacity={} alpha={} progress={} material={} blend={} depthTest={} depthWrite={} nativeModel=1",
            projected->opacity, shader.color->a, progress,
            info ? info->mHashedName->getString() : std::string{},
            material && material->blendStateDescription->enableBlend,
            material && material->depthStencilStateDescription->depthTestEnabled,
            material ? static_cast<unsigned int>(material->depthStencilStateDescription->depthWriteMask) : 0U
        );
    }
    origin(screenContext, progress);
}

LL_TYPE_INSTANCE_HOOK(
    LevelRendererPlayerRenderHitSelectHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::renderHitSelect,
    void,
    BaseActorRenderContext& renderContext,
    BlockSource&            region,
    BlockPos const&         pos,
    bool                    fancyGraphics
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard) {
        origin(renderContext, region, pos, fancyGraphics);
        return;
    }
    try {
        if (shouldSuppressProjectionHitSelect(pos)) return;
    } catch (std::exception const& exception) {
        app::reportNativeCallbackFailure("projection hit selection", exception.what());
    } catch (...) {
        app::reportNativeCallbackFailure("projection hit selection", "unknown C++ exception");
    }
    origin(renderContext, region, pos, fancyGraphics);
}

LL_TYPE_INSTANCE_HOOK(
    LevelRendererPlayerRenderBlockEntitiesHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::$renderBlockEntities,
    void,
    BaseActorRenderContext& renderContext,
    bool                      renderAlphaLayer
) {
    app::hook_lifecycle::DetourGuard guard;
    origin(renderContext, renderAlphaLayer);
    if (!guard) return;
    app::invokeNativeCallback([&] {
        // The first install attempt can happen before Minecraft exposes a usable
        // swap chain. Keep retrying from the render path, which is active even
        // while the menu is hidden and does not depend on Present already being
        // hooked.
        (void)overlay::ensureInstalled();
        renderProjectionFrame(renderContext, renderAlphaLayer);
    }, [](char const* reason) noexcept {
        app::reportNativeCallbackFailure("projection rendering", reason);
    });
}

} // namespace

namespace {

bool gHitSelectHookInstalled{};
bool gBlockEntitiesHookInstalled{};
bool gPistonArmHookInstalled{};

} // namespace

bool installProjectionRenderHooks() {
    gPistonArmHookInstalled = ProjectedPistonArmRenderHook::hook() == 0;
    if (!gPistonArmHookInstalled) return false;
    gHitSelectHookInstalled = LevelRendererPlayerRenderHitSelectHook::hook() == 0;
    if (!gHitSelectHookInstalled) {
        if (ProjectedPistonArmRenderHook::unhook()) gPistonArmHookInstalled = false;
        return false;
    }

    gBlockEntitiesHookInstalled =
        LevelRendererPlayerRenderBlockEntitiesHook::hook() == 0;
    if (!gBlockEntitiesHookInstalled) {
        if (LevelRendererPlayerRenderHitSelectHook::unhook()) {
            gHitSelectHookInstalled = false;
        }
        if (ProjectedPistonArmRenderHook::unhook()) gPistonArmHookInstalled = false;
        return false;
    }
    return true;
}

bool uninstallProjectionRenderHooks() {
    bool ok = true;
    if (gBlockEntitiesHookInstalled) {
        if (LevelRendererPlayerRenderBlockEntitiesHook::unhook()) {
            gBlockEntitiesHookInstalled = false;
        } else {
            ok = false;
        }
    }
    if (gHitSelectHookInstalled) {
        if (LevelRendererPlayerRenderHitSelectHook::unhook()) {
            gHitSelectHookInstalled = false;
        } else {
            ok = false;
        }
    }
    if (gPistonArmHookInstalled) {
        if (ProjectedPistonArmRenderHook::unhook()) gPistonArmHookInstalled = false;
        else ok = false;
    }
    return ok;
}

} // namespace lholo::projection::detail
