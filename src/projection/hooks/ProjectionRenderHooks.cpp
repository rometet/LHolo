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

namespace lholo::projection::detail {
namespace {

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
    } catch (...) {
        // A projection query must never unwind through Minecraft's render hook.
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
    try {
        // The first install attempt can happen before Minecraft exposes a usable
        // swap chain. Keep retrying from the render path, which is active even
        // while the menu is hidden and does not depend on Present already being
        // hooked.
        (void)overlay::ensureInstalled();
        renderProjectionFrame(renderContext, renderAlphaLayer);
    } catch (...) {
        // Rendering LHolo is optional; an exception here must not cross into
        // Minecraft's renderer or take the game down.
    }
}

} // namespace

namespace {

bool gHitSelectHookInstalled{};
bool gBlockEntitiesHookInstalled{};

} // namespace

bool installProjectionRenderHooks() {
    gHitSelectHookInstalled = LevelRendererPlayerRenderHitSelectHook::hook() == 0;
    if (!gHitSelectHookInstalled) return false;

    gBlockEntitiesHookInstalled =
        LevelRendererPlayerRenderBlockEntitiesHook::hook() == 0;
    if (!gBlockEntitiesHookInstalled) {
        if (LevelRendererPlayerRenderHitSelectHook::unhook()) {
            gHitSelectHookInstalled = false;
        }
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
    return ok;
}

} // namespace lholo::projection::detail
