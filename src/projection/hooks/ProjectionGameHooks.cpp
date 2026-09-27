// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/hooks/ProjectionGameHooks.h"

#include "app/HookLifecycle.h"

#include "overlay/ImGuiOverlay.h"
#include "plugin/LHolo.h"
#include "projection/world/ProjectionVirtualWorld.h"
#include "structure/StructureLoader.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string_view>
#include <variant>

#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/world/level/ActorBlockSyncMessage.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/BlockActor.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"

namespace lholo::projection::detail {
namespace {

struct ProjectionGameHookStatus {
    bool getBlock{};
    bool getBlockLayer{};
    bool getLiquidBlock{};
    bool getBlockEntity{};
    bool setBlock{};
    bool setBlockWithActor{};
    bool sendToServer{};
    bool send{};
};

std::mutex gGameHookMutex;
ProjectionGameHookStatus gGameHookStatus{};

template <class Hook>
bool installTracked(bool& installed) {
    if (installed) return true;
    if (Hook::hook() != 0) return false;
    installed = true;
    return true;
}

template <class Hook>
bool uninstallTracked(bool& installed) {
    if (!installed) return true;
    if (!Hook::unhook()) return false;
    installed = false;
    return true;
}

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

bool isMenuCommand(std::string_view message) {
    constexpr std::string_view command{"lholo"};
    if (message.size() != command.size()) return false;
    for (std::size_t index = 0; index < command.size(); ++index) {
        auto character = message[index];
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        if (character != command[index]) return false;
    }
    return true;
}

bool filterProjectionPacket(Packet& packet) {
    if (packet.getId() != MinecraftPacketIds::Text) return false;
    auto& textPacket = static_cast<TextPacket&>(packet);
    auto const* chat = std::get_if<TextPacketPayload::AuthorAndMessage>(
        &textPacket.mBody.get()
    );
    if (!chat || chat->mType != TextPacketType::Chat
        || !isMenuCommand(chat->mMessage.get())) return false;

    if (overlay::ensureInstalled()) {
        structure::requestOpenGui();
    } else {
        logger().error("Could not initialize the injected ImGui overlay");
    }
    return true;
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Block const& (BlockSource::*)(BlockPos const&) const>(&BlockSource::$getBlock),
    Block const&,
    BlockPos const& position
) {
    if (!app::hook_lifecycle::isRunning()) return origin(position);
    if (auto const* block = findTessellationBlock(position)) return *block;
    return origin(position);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockLayerHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Block const& (BlockSource::*)(BlockPos const&, uint) const>(&BlockSource::$getBlock),
    Block const&,
    BlockPos const& position,
    uint layer
) {
    if (!app::hook_lifecycle::isRunning()) return origin(position, layer);
    if (layer == 0) {
        if (auto const* block = findTessellationBlock(position)) return *block;
    } else if (layer == 1) {
        if (auto const* liquid = findTessellationLiquid(position)) return *liquid;
    }
    return origin(position, layer);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetLiquidBlockHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    &BlockSource::$getLiquidBlock,
    Block const&,
    BlockPos const& position
) {
    if (!app::hook_lifecycle::isRunning()) return origin(position);
    if (auto const* liquid = findTessellationLiquid(position)) return *liquid;
    return origin(position);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockEntityHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<BlockActor const* (BlockSource::*)(BlockPos const&) const>(&BlockSource::$getBlockEntity),
    BlockActor const*,
    BlockPos const& position
) {
    if (!app::hook_lifecycle::isRunning()) return origin(position);
    if (auto const* actor = findTessellationBlockActor(position)) return actor;
    return origin(position);
}

// BlockType::connectionUpdate recomputes flattened connection geometry
// correctly for every block family, but it also writes the recomputed block
// into the region it is handed. While a ScopedRegionWriteSuppression is
// active on this thread the write is swallowed: the caller keeps only the
// returned block, and the real world never sees the projected blocks.
LL_TYPE_INSTANCE_HOOK(
    BlockSourceSetBlockHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    &BlockSource::$setBlock,
    bool,
    BlockPos const&                 position,
    Block const&                    block,
    int                             updateFlags,
    ActorBlockSyncMessage const*    syncMsg,
    BlockChangeContext const&       changeSourceContext
) {
    if (!app::hook_lifecycle::isRunning()) {
        return origin(position, block, updateFlags, syncMsg, changeSourceContext);
    }
    if (regionWritesSuppressed()) return true;
    return origin(position, block, updateFlags, syncMsg, changeSourceContext);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceSetBlockWithActorHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<
        bool (BlockSource::*)(
            BlockPos const&, Block const&, int, std::shared_ptr<BlockActor>,
            ActorBlockSyncMessage const*, BlockChangeContext const&
        )>(&BlockSource::setBlock),
    bool,
    BlockPos const&                 position,
    Block const&                    block,
    int                             updateFlags,
    std::shared_ptr<BlockActor>     blockEntity,
    ActorBlockSyncMessage const*    syncMsg,
    BlockChangeContext const&       changeSourceContext
) {
    if (!app::hook_lifecycle::isRunning()) {
        return origin(position, block, updateFlags, blockEntity, syncMsg, changeSourceContext);
    }
    if (regionWritesSuppressed()) return true;
    return origin(position, block, updateFlags, blockEntity, syncMsg, changeSourceContext);
}

LL_TYPE_INSTANCE_HOOK(
    LoopbackPacketSenderSendToServerHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$sendToServer,
    void,
    Packet& packet
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard) {
        origin(packet);
        return;
    }
    if (filterProjectionPacket(packet)) return;
    origin(packet);
}

LL_TYPE_INSTANCE_HOOK(
    LoopbackPacketSenderSendHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$send,
    void,
    Packet& packet
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard) {
        origin(packet);
        return;
    }
    if (filterProjectionPacket(packet)) return;
    origin(packet);
}

} // namespace

bool installProjectionGameHooks() {
    std::lock_guard lock(gGameHookMutex);
    return installTracked<BlockSourceGetBlockHook>(gGameHookStatus.getBlock)
        && installTracked<BlockSourceGetBlockLayerHook>(gGameHookStatus.getBlockLayer)
        && installTracked<BlockSourceGetBlockEntityHook>(gGameHookStatus.getBlockEntity)
        && installTracked<BlockSourceGetLiquidBlockHook>(gGameHookStatus.getLiquidBlock)
        && installTracked<BlockSourceSetBlockHook>(gGameHookStatus.setBlock)
        && installTracked<BlockSourceSetBlockWithActorHook>(gGameHookStatus.setBlockWithActor)
        && installTracked<LoopbackPacketSenderSendToServerHook>(gGameHookStatus.sendToServer)
        && installTracked<LoopbackPacketSenderSendHook>(gGameHookStatus.send);
}

bool uninstallProjectionGameHooks() {
    std::lock_guard lock(gGameHookMutex);
    bool ok = true;
    ok = uninstallTracked<LoopbackPacketSenderSendHook>(gGameHookStatus.send) && ok;
    ok = uninstallTracked<LoopbackPacketSenderSendToServerHook>(gGameHookStatus.sendToServer) && ok;
    ok = uninstallTracked<BlockSourceSetBlockWithActorHook>(gGameHookStatus.setBlockWithActor) && ok;
    ok = uninstallTracked<BlockSourceSetBlockHook>(gGameHookStatus.setBlock) && ok;
    ok = uninstallTracked<BlockSourceGetLiquidBlockHook>(gGameHookStatus.getLiquidBlock) && ok;
    ok = uninstallTracked<BlockSourceGetBlockEntityHook>(gGameHookStatus.getBlockEntity) && ok;
    ok = uninstallTracked<BlockSourceGetBlockLayerHook>(gGameHookStatus.getBlockLayer) && ok;
    ok = uninstallTracked<BlockSourceGetBlockHook>(gGameHookStatus.getBlock) && ok;
    return ok;
}

} // namespace lholo::projection::detail
