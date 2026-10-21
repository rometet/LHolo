#include "place/PlacementRotationDelivery.h"
#include "place/PlacementState.h"
#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "projection/Projection.h"
#include "structure/StructureLoader.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/entity/components/PackedItemUseLegacyInventoryTransaction.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/packet/InventoryTransactionPacketPayload.h"
#include "mc/network/packet/LegacySetSlot.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <thread>

namespace lholo::place::detail {
namespace {
struct PendingRotation {
    LocalPlayer* player;
    std::thread::id thread;
    std::uint64_t epoch,expires;
    BlockPos cell;
    std::int64_t cellKey;
    unsigned ghostId;
    int slot;
    ItemStack item;
    float pitch,yaw;
    std::unique_ptr<InventoryTransactionPacketPayload> payload;
};
std::mutex pendingMutex;
std::optional<PendingRotation> pending;
std::atomic_bool available{};

bool stillValid(PendingRotation const& p,ClientInstance& client) {
    auto& state=PlacementState::getInstance();
    if (GetTickCount64()>=p.expires || p.epoch!=state.manualInputEpoch()
        || client.getLocalPlayer()!=p.player || !client.isInGameInputEnabled()
        || structure::isGuiVisible()) return false;
    if (!state.enabled() && !state.manualMode() && !state.rangeEnabled()) return false;
    auto const now=GetTickCount64();
    // The shared legacy path can fulfill a tap while this request is deferred.
    // Respect the existing Manual initial/repeat timing before forwarding it.
    if (state.manualMode() && !state.manualPlaceRequested()
        && (!state.manualHeld() || now-state.manualPressAt()<150
            || now-state.lastManualPlaceAt()<120)) return false;
    if (state.recentPlacementActive(p.cellKey,now)) return false;
    if (!state.manualMode() && state.autoPlacementSuppressionsActive(now)
        && state.autoPlacementSuppressed(p.cellKey,now)) return false;
    if (p.player->getSelectedItemSlot()!=p.slot) return false;
    auto const& item=p.player->getInventory().getItem(p.slot);
    if (state.manualMode() && state.manualPlacementItemAllowed(item.getTypeName())) return false;
    if (item.isNull() || item.getIdAux()!=p.item.getIdAux() || item.mCount!=p.item.mCount
        || !item.matchesNetIdVariant(p.item)) return false;
    auto const& region=p.player->getDimensionBlockSource();
    if (!region.getBlock(p.cell).isAir()) return false;
    auto const ghost=projection::queryProjection(*p.player,p.cell);
    auto const* tx=std::get_if<ItemUseInventoryTransaction>(&p.payload->mVariantTransaction.get());
    return ghost.block && ghost.missing && ghost.block->mNetworkId==p.ghostId && tx
        && region.getBlock(tx->mPos).mNetworkId==tx->mTargetBlockId;
}

LL_TYPE_INSTANCE_HOOK(RotationAuthInputHook,ll::memory::HookPriority::Normal,
    LoopbackPacketSender,&LoopbackPacketSender::$sendToServer,void,Packet& packet) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard || !available.load(std::memory_order_acquire)
        || packet.getId()!=MinecraftPacketIds::PlayerAuthInputPacket) { origin(packet);return; }
    auto client=ll::service::getClientInstance();
    if (!client || &client->getPacketSender()!=static_cast<PacketSender*>(this)) { origin(packet);return; }
    // Another admitted Range candidate may have used the legacy path since
    // queue admission. Preserve the shared minimum interval across both paths.
    if (GetTickCount64()<PlacementState::getInstance().nextPlaceAt()) { origin(packet);return; }
    auto& input=static_cast<PlayerAuthInputPacket&>(packet);
    using Input=PlayerAuthInputPacketPayload::InputData;
    // Never overwrite native interaction/stack request/block-action ownership.
    if (input.mItemUseTransaction.get() || input.mItemStackRequest.get()
        || input.mInputData.get().contains(Input::PerformItemInteraction)
        || input.mInputData.get().contains(Input::PerformItemStackRequest)
        || input.mInputData.get().contains(Input::PerformBlockActions)) { origin(packet);return; }
    std::optional<PendingRotation> request;
    {
        std::lock_guard lock(pendingMutex);
        if (pending && pending->thread==std::this_thread::get_id()) {
            request=std::move(pending);pending.reset();
        }
    }
    if (!request) { origin(packet);return; }
    bool valid=false;
    app::invokeNativeCallback([&] { valid=stillValid(*request,*client); },
        [](char const* reason) noexcept { app::reportNativeCallbackFailure("rotation placement validation",reason); });
    if (!valid) { origin(packet);return; }
    auto const savedRotation=input.mInteractRotation.get();
    auto const savedFlags=input.mInputData.get();
    bool attached=false;
    app::invokeNativeCallback([&] {
        auto& payload=*request->payload;
        auto const* tx=std::get_if<ItemUseInventoryTransaction>(&payload.mVariantTransaction.get());
        if (!tx) return;
        // Preserve native legacy request ID / slot actions / transaction fields.
        input.mItemUseTransaction=std::make_unique<PackedItemUseLegacyInventoryTransaction>(
            PackedItemUseLegacyInventoryTransaction{payload.mLegacyRequestId.get(),
                payload.mLegacySetItemSlots.get(),*tx});
        attached=true;
        input.mInteractRotation.get()={request->pitch,request->yaw};
        input.mInputData.get().insert(Input::PerformItemInteraction);
    },[](char const* reason) noexcept { app::reportNativeCallbackFailure("rotation placement attach",reason); });
    // Forward one fresh native input: its tick, movement, camera and main/head
    // rotations are unchanged. No invented movement packet or duplicate tick.
    auto const forwarded=app::invokeNativeCallback([&] { origin(packet); },
        [](char const* reason) noexcept { app::reportNativeCallbackFailure("rotation placement forward",reason); });
    if (attached) input.mItemUseTransaction.reset();
    input.mInteractRotation.get()=savedRotation;
    input.mInputData.get()=savedFlags;
    if (attached) {
        // A native forwarding exception may occur after serialization. Bound
        // that ambiguous attempt too; never immediately replay an uncertain use.
        auto& state=PlacementState::getInstance();auto const now=GetTickCount64();
        state.recordRecentPlacement(request->cellKey,now,now+500);
        state.setNextPlaceAt(now+40);
        if (forwarded && state.manualMode() && state.manualInputEpoch()==request->epoch) {
            state.setLastManualPlaceAt(now);state.setManualPlaceRequested(false);
        }
        // Forwarded is not server acceptance; existing 500ms retry remains.
    }
}
}

void queueRotationPlacement(LocalPlayer& player,BlockPos const& cell,std::int64_t cellKey,
    unsigned ghostId,int slot,ItemStack const& item,float yaw,std::unique_ptr<ComplexInventoryTransaction> tx) {
    if (!available.load(std::memory_order_acquire) || !tx || !std::isfinite(yaw)
        || !std::isfinite(player.getRotation().x) || slot<0 || slot>=9) return;
    std::lock_guard lock(pendingMutex);
    auto& state=PlacementState::getInstance();auto const now=GetTickCount64();
    if (pending && pending->expires>now && pending->epoch==state.manualInputEpoch()) return;
    pending.reset();
    auto payload=std::make_unique<InventoryTransactionPacketPayload>(std::move(tx),true);
    pending.emplace(PendingRotation{&player,std::this_thread::get_id(),state.manualInputEpoch(),now+250,
        cell,cellKey,ghostId,slot,item,player.getRotation().x,yaw,std::move(payload)});
}

bool installRotationDeliveryHook() {
    auto const ok=RotationAuthInputHook::hook()==0;
    available.store(ok,std::memory_order_release);return ok;
}
bool uninstallRotationDeliveryHook() {
    available.store(false,std::memory_order_release);
    { std::lock_guard lock(pendingMutex);pending.reset(); }
    return RotationAuthInputHook::unhook();
}
}
