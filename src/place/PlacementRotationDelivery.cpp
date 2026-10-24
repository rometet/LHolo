#include "place/PlacementRotationDelivery.h"
#include "place/FlexiblePlacementBridge.h"
#include "place/PlacementState.h"
#include "place/EasyPlaceTelemetry.h"
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
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/inventory/transaction/ComplexInventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryAction.h"
#include "mc/world/inventory/transaction/InventoryTransactionItemGroup.h"
#include "mc/world/inventory/transaction/ItemUseInventoryTransaction.h"
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
    bool requireNativeRotationSync{};
    std::unique_ptr<InventoryTransactionPacketPayload> payload;
    bool external{};
    BlockPos support{};
    std::uint32_t targetBlockId{};
    uchar face{};
};
std::mutex pendingMutex;
std::optional<PendingRotation> pending;
std::atomic_bool available{};

std::int64_t bridgeCellKey(BlockPos const& p) noexcept {
    auto const x = static_cast<std::uint64_t>(static_cast<std::int64_t>(p.x)) & 0x3FFFFFFULL;
    auto const z = static_cast<std::uint64_t>(static_cast<std::int64_t>(p.z)) & 0x3FFFFFFULL;
    auto const y = static_cast<std::uint64_t>(static_cast<std::int64_t>(p.y)) & 0xFFFULL;
    return static_cast<std::int64_t>((x << 38U) | (z << 12U) | y);
}

template <class T>
BlockPos transactionSupport(T const& transaction) {
    if constexpr (requires { transaction.mPos.get(); }) {
        return transaction.mPos.get();
    } else {
        return transaction.mPos;
    }
}

template <class T>
auto transactionFace(T const& transaction) {
    if constexpr (requires { transaction.mFace; }) {
        return transaction.mFace;
    } else {
        return transaction.face;
    }
}

// Strict stair uses MUST be attached to a genuine native AuthInput that
// already carries the same viewing direction. Never rewrite player/movement
// rotation or accept the local planner's yaw without this same-tick proof.
bool stillValid(PendingRotation const& p,ClientInstance& client) {
    auto& state=PlacementState::getInstance();
    if (GetTickCount64()>=p.expires || p.epoch!=state.manualInputEpoch()
        || client.getLocalPlayer()!=p.player || !client.isInGameInputEnabled()
        || structure::isGuiVisible()) return false;
    if (!p.external && !state.enabled() && !state.manualMode() && !state.rangeEnabled()) return false;
    auto const now=GetTickCount64();
    // The shared legacy path can fulfill a tap while this request is deferred.
    // Respect the existing Manual initial/repeat timing before forwarding it.
    if (!p.external && state.manualMode() && !state.manualPlaceRequested()
        && (!state.manualHeld() || now-state.manualPressAt()<100
            || now-state.lastManualPlaceAt()<50)) return false;
    if (state.recentPlacementActive(p.cellKey,now)) return false;
    if (!p.external && !state.manualMode() && state.autoPlacementSuppressionsActive(now)
        && state.autoPlacementSuppressed(p.cellKey,now)) return false;
    if (p.player->getSelectedItemSlot()!=p.slot) return false;
    auto const& item=p.player->getInventory().getItem(p.slot);
    if (state.manualMode() && state.manualPlacementItemAllowed(item.getTypeName())) return false;
    if (item.isNull() || item.getIdAux()!=p.item.getIdAux() || item.mCount!=p.item.mCount
        || !item.matchesNetIdVariant(p.item)) return false;
    auto const& region=p.player->getDimensionBlockSource();
    if (!region.getBlock(p.cell).isAir()) return false;
    auto const* tx=std::get_if<ItemUseInventoryTransaction>(&p.payload->mVariantTransaction.get());
    if (!tx) return false;
    if (p.external) {
        auto const support = transactionSupport(*tx);
        return support == p.support
            && transactionFace(*tx) == p.face
            && tx->mTargetBlockId == p.targetBlockId
            && !region.getBlock(p.support).isAir()
            && region.getBlock(p.support).mNetworkId == p.targetBlockId;
    }
    auto const ghost=projection::queryPlacementProjection(*p.player,p.cell);
    return ghost.block && ghost.missing && ghost.block->mNetworkId==p.ghostId
        && region.getBlock(transactionSupport(*tx)).mNetworkId==tx->mTargetBlockId;
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
    // R19: strict stair input must not conflict with an ongoing native
    // action (screen, movement flag, item use); that gate is above. We now
    // synchronize the two authoritative rotation fields for this one packet,
    // not the local camera, actor, head, or subsequent network ticks.
    bool valid=false;
    app::invokeNativeCallback([&] { valid=stillValid(*request,*client); },
        [](char const* reason) noexcept { app::reportNativeCallbackFailure("rotation placement validation",reason); });
    if (!valid) { origin(packet);return; }
    auto const savedRotation=input.mInteractRotation.get();
    auto const savedPlayerRotation=input.mRot.get();
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
        // The two network rotations must agree when the server evaluates
        // stairs. All other gameplay/local facing stays unchanged.
        if (request->requireNativeRotationSync)
            input.mRot.get()={request->pitch,request->yaw};
        input.mInteractRotation.get()={request->pitch,request->yaw};
        input.mInputData.get().insert(Input::PerformItemInteraction);
    },[](char const* reason) noexcept { app::reportNativeCallbackFailure("rotation placement attach",reason); });
    // Forward one fresh native input: its tick, movement, camera and main/head
    // rotations are unchanged. No invented movement packet or duplicate tick.
    auto const forwarded=app::invokeNativeCallback([&] { origin(packet); },
        [](char const* reason) noexcept { app::reportNativeCallbackFailure("rotation placement forward",reason); });
    if (attached) input.mItemUseTransaction.reset();
    input.mInteractRotation.get()=savedRotation;
    input.mRot.get()=savedPlayerRotation;
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

extern "C" __declspec(dllexport)
std::uint32_t lholo_flexible_placement_v1_get_status() noexcept {
    auto const& state = lholo::place::detail::PlacementState::getInstance();
    std::uint32_t result{};
    if (state.enabled()) result |= lholo::flexible_placement_abi::EasyPlaceOwnsPlacement;
    if (state.manualMode()) result |= lholo::flexible_placement_abi::ManualOwnsPlacement;
    if (state.rangeEnabled()) result |= lholo::flexible_placement_abi::RangeOwnsPlacement;
    return result;
}

extern "C" __declspec(dllexport)
std::uint32_t lholo_flexible_placement_v1_queue(
    lholo::flexible_placement_abi::FlexiblePlacementRequest request) noexcept {
    using namespace lholo::flexible_placement_abi;
    try {
        if (request.version != kVersion || request.face >= 6
            || request.selectedSlot < 0 || request.selectedSlot >= 9
            || !std::isfinite(request.relativeHitX)
            || !std::isfinite(request.relativeHitY)
            || !std::isfinite(request.relativeHitZ)
            || !std::isfinite(request.pitch) || !std::isfinite(request.yaw)
            || std::fabs(request.pitch) > 90.F) return QueueRejected;
        if (lholo_flexible_placement_v1_get_status() != 0) return QueueBusy;
        auto client = ll::service::getClientInstance();
        if (!client) return QueueRejected;
        auto* player = client->getLocalPlayer();
        if (!player) return QueueRejected;
        auto const& item = player->getInventory().getItem(request.selectedSlot);
        if (item.isNull() || item.mCount == 0 || !item.mBlock
            || item.getId() != request.itemId
            || item.getIdAux() != request.itemAux
            || item.mCount != request.itemCount) return QueueRejected;
        auto const local = Vec3{request.relativeHitX,request.relativeHitY,request.relativeHitZ};
        auto const inRange = [](float value) { return value >= 0.0F && value <= 1.0F; };
        if (!inRange(local.x) || !inRange(local.y) || !inRange(local.z)) return QueueRejected;
        auto const boundary = [](float value) {
            return std::fabs(value) < 0.0001F || std::fabs(value - 1.0F) < 0.0001F;
        };
        if ((request.face == 0 || request.face == 1) && !boundary(local.y)) return QueueRejected;
        if ((request.face == 2 || request.face == 3) && !boundary(local.z)) return QueueRejected;
        if ((request.face == 4 || request.face == 5) && !boundary(local.x)) return QueueRejected;
        auto const eye = player->getEyePos();
        Vec3 const worldHit{
            static_cast<float>(request.supportX) + local.x,
            static_cast<float>(request.supportY) + local.y,
            static_cast<float>(request.supportZ) + local.z,
        };
        auto const dx=worldHit.x-eye.x,dy=worldHit.y-eye.y,dz=worldHit.z-eye.z;
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)
            || dx*dx+dy*dy+dz*dz > 36.0F) return QueueRejected;
        auto transactionBase = ComplexInventoryTransaction::fromType(
            ComplexInventoryTransaction::Type::ItemUseTransaction, InventoryTransaction{}
        );
        if (!transactionBase) return QueueRejected;
        auto& transaction = static_cast<ItemUseInventoryTransaction&>(*transactionBase);
        transaction.mType = ComplexInventoryTransaction::Type::ItemUseTransaction;
        transaction.mActionType = ItemUseInventoryTransaction::ActionType::Place;
        transaction.mTriggerType = ItemUseInventoryTransaction::TriggerType::PlayerInput;
        transaction.mPos = BlockPos{request.supportX,request.supportY,request.supportZ};
        transaction.mFace = static_cast<uchar>(request.face);
        transaction.mSlot = request.selectedSlot;
        transaction.mHand = HandSlot::Mainhand;
        transaction.mFromPos = player->getPosition();
        transaction.mClickPos = local;
        transaction.mClientPredictedResult = ItemUseInventoryTransaction::PredictedResult::Success;
        transaction.mClientCooldownState = ItemUseInventoryTransaction::ClientCooldownState::Off;
        transaction.mTargetBlockId = request.targetBlockId;
        transaction.setSelectedItem(item);
        transaction.mItem.get().mIncludeNetIds = true;
        auto const destination = BlockPos{
            request.supportX + (request.face == 5 ? 1 : request.face == 4 ? -1 : 0),
            request.supportY + (request.face == 1 ? 1 : request.face == 0 ? -1 : 0),
            request.supportZ + (request.face == 3 ? 1 : request.face == 2 ? -1 : 0),
        };
        return lholo::place::detail::queueExternalRotationPlacement(
            *player,destination,BlockPos{request.supportX,request.supportY,request.supportZ},
            request.targetBlockId,static_cast<uchar>(request.face),request.selectedSlot,
            item,request.pitch,request.yaw,std::move(transactionBase))
            ? QueueAccepted : QueueRejected;
    } catch (...) {
        return QueueRejected;
    }
}

bool queueRotationPlacement(LocalPlayer& player,BlockPos const& cell,std::int64_t cellKey,
    unsigned ghostId,int slot,ItemStack const& item,float pitch,float yaw,
    std::unique_ptr<ComplexInventoryTransaction> tx,
    bool requireNativeRotationSync) {
    // Both angles must be validated before borrowing any native packet state.
    // Pitch is the Bedrock interaction pitch in [-90,90]; do not set camera,
    // body/head yaw or invent a second network tick.
    if (!available.load(std::memory_order_acquire) || !tx
        || !std::isfinite(pitch) || !std::isfinite(yaw)
        || std::fabs(pitch)>90.F || slot<0 || slot>=9) return false;
    std::lock_guard lock(pendingMutex);
    auto& state=PlacementState::getInstance();auto const now=GetTickCount64();
    // An existing pending AuthInput placement owns this tick; a second
    // legacy placement would change the selected stack and invalidate it.
    if (pending && pending->expires>now && pending->epoch==state.manualInputEpoch()) return true;
    pending.reset();
    auto payload=std::make_unique<InventoryTransactionPacketPayload>(std::move(tx),true);
    pending.emplace(PendingRotation{&player,std::this_thread::get_id(),state.manualInputEpoch(),now+250,
        cell,cellKey,ghostId,slot,item,pitch,yaw,requireNativeRotationSync,
        std::move(payload)});
    return true;
}

bool queueExternalRotationPlacement(LocalPlayer& player, BlockPos const& cell,
    BlockPos const& support, std::uint32_t targetBlockId, uchar face, int slot,
    ItemStack const& item, float pitch, float yaw,
    std::unique_ptr<ComplexInventoryTransaction> tx) {
    if (!available.load(std::memory_order_acquire) || !tx
        || face >= 6 || slot < 0 || slot >= 9
        || !std::isfinite(pitch) || !std::isfinite(yaw)
        || std::fabs(pitch) > 90.F) return false;
    auto client = ll::service::getClientInstance();
    if (!client || client->getLocalPlayer() != &player
        || player.getSelectedItemSlot() != slot) return false;
    auto const& current = player.getInventory().getItem(slot);
    if (current.isNull() || !current.matchesNetIdVariant(item)) return false;
    auto& region = player.getDimensionBlockSource();
    if (!region.getBlock(cell).isAir() || region.getBlock(support).isAir()
        || region.getBlock(support).mNetworkId != targetBlockId) return false;
    BlockPos const expected{
        support.x + (face == 5 ? 1 : face == 4 ? -1 : 0),
        support.y + (face == 1 ? 1 : face == 0 ? -1 : 0),
        support.z + (face == 3 ? 1 : face == 2 ? -1 : 0),
    };
    if (expected != cell) return false;

    std::lock_guard lock(pendingMutex);
    auto& state=PlacementState::getInstance();
    if (state.enabled() || state.manualMode() || state.rangeEnabled()) return false;
    auto const now=GetTickCount64();
    if (pending && pending->expires>now && pending->epoch==state.manualInputEpoch()) return false;
    pending.reset();
    auto payload=std::make_unique<InventoryTransactionPacketPayload>(std::move(tx),true);
    pending.emplace(PendingRotation{&player,std::this_thread::get_id(),state.manualInputEpoch(),now+250,
        cell,bridgeCellKey(cell),targetBlockId,slot,item,pitch,yaw,true,std::move(payload),
        true,support,targetBlockId,face});
    return true;
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
