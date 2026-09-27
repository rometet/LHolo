// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/hooks/ProjectionRenderHooks.h"

#include "app/HookLifecycle.h"

#include "overlay/ImGuiOverlay.h"
#include "projection/runtime/ProjectionRenderFrame.h"

#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "ll/api/memory/Hook.h"

#include <mutex>

namespace lholo::projection::detail {
namespace {

std::mutex gRenderHookMutex;
bool gHitSelectHookInstalled{};
bool gBlockEntitiesHookInstalled{};

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
    if (shouldSuppressProjectionHitSelect(pos)) return;
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

    // The first install attempt can happen before Minecraft exposes a usable
    // swap chain. Keep retrying from the render path, which is active even
    // while the menu is hidden and does not depend on Present already being
    // hooked. ensureInstalled() is also lifecycle-gated, so a late return from
    // origin() cannot resurrect the overlay after disable has begun.
    (void)overlay::ensureInstalled();
    renderProjectionFrame(renderContext, renderAlphaLayer);
}

} // namespace

bool installProjectionRenderHooks() {
    std::lock_guard lock(gRenderHookMutex);
    bool installedHitThisCall = false;

    if (!gHitSelectHookInstalled) {
        if (LevelRendererPlayerRenderHitSelectHook::hook() != 0) return false;
        gHitSelectHookInstalled = true;
        installedHitThisCall = true;
    }

    if (!gBlockEntitiesHookInstalled) {
        if (LevelRendererPlayerRenderBlockEntitiesHook::hook() != 0) {
            if (installedHitThisCall && LevelRendererPlayerRenderHitSelectHook::unhook()) {
                gHitSelectHookInstalled = false;
            }
            return false;
        }
        gBlockEntitiesHookInstalled = true;
    }
    return true;
}

bool uninstallProjectionRenderHooks() {
    std::lock_guard lock(gRenderHookMutex);
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
    return ok;
}

} // namespace lholo::projection::detail
