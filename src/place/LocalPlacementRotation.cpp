#include "place/LocalPlacementRotation.h"
#include "app/HookLifecycle.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/world/inventory/transaction/ItemUseInventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryTransactionError.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/HandSlot.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>

namespace lholo::place::detail {
namespace {
struct LocalReceipt {
    std::uint64_t runtimeId{}, expires{}, ownerToken{}, ownerEpoch{};
    Player const* clientIdentity{}; // Address comparison only; never dereferenced on server thread.
    BlockPos support{}, destination{};
    std::uint32_t targetBlockId{};
    int dimension{}, slot{}, itemId{}, itemAux{}, count{};
    unsigned char face{};
    Vec3 hit{}, from{};
    float pitch{}, yaw{};
};
std::mutex receiptMutex;
std::array<std::optional<LocalReceipt>, 8> receipts;
std::atomic_bool receiverAvailable{};
std::atomic_uint receiverTraceCount{};

bool samePoint(Vec3 const& a,Vec3 const& b) noexcept {
    return std::fabs(a.x-b.x)<0.00001F && std::fabs(a.y-b.y)<0.00001F
        && std::fabs(a.z-b.z)<0.00001F;
}

std::optional<LocalReceipt> claimReceipt(ItemUseInventoryTransaction const& tx, Player& player) {
    // This path must never change a client camera or another player's state.
    if (player.isClientSide() || tx.mActionType != ItemUseInventoryTransaction::ActionType::Place
        || tx.mHand != HandSlot::Mainhand) return std::nullopt;
    auto const runtimeId=static_cast<std::uint64_t>(player.getRuntimeID());
    auto const now=GetTickCount64();
    auto const slot=player.getSelectedItemSlot();
    if(slot<0 || slot>=9 || tx.mSlot!=slot) return std::nullopt;
    auto const& item=player.getInventory().getItem(slot);
    if(item.isNull()) return std::nullopt;
    std::lock_guard lock(receiptMutex);
    for(auto& entry:receipts) {
        if(entry && entry->expires<=now) entry.reset();
        if(!entry) continue;
        auto const& r=*entry;
        if(r.runtimeId!=runtimeId || r.clientIdentity==&player
            || r.dimension!=static_cast<int>(player.getDimensionId())
            || r.slot!=slot || r.itemId!=item.getId() || r.itemAux!=item.getIdAux()
            || r.count!=item.mCount || r.support!=tx.mPos.get()
            || r.face!=tx.mFace || r.targetBlockId!=tx.mTargetBlockId
            || !samePoint(r.hit,tx.mClickPos.get()) || !samePoint(r.from,tx.mFromPos.get())) continue;
        auto claimed=entry;
        entry.reset(); // Consume once before entering vanilla (including re-entrant paths).
        return claimed;
    }
    return std::nullopt;
}

struct ServerRotationRestore {
    Player& player;
    Vec2 saved;
    ~ServerRotationRestore() noexcept {const_cast<Vec2&>(player.getRotation())=saved;}
};

LL_TYPE_INSTANCE_HOOK(LocalPlacementUseRotationHook,ll::memory::HookPriority::High,
    ItemUseInventoryTransaction,&ItemUseInventoryTransaction::$handle,
    InventoryTransactionError,Player& player,bool isSenderAuthority) {
    app::hook_lifecycle::DetourGuard guard;
    if(!guard || !receiverAvailable.load(std::memory_order_acquire))
        return origin(player,isSenderAuthority);
    std::optional<LocalReceipt> request;
    try {request=claimReceipt(*this,player);} catch (...) {}
    if(!request) return origin(player,isSenderAuthority);
    // Integrated server evaluates BlockItem placement against this ServerPlayer,
    // not the LocalPlayer packet object. Borrow the selected orientation for this
    // ONE validated vanilla transaction and restore even when vanilla throws.
    auto const previous=player.getRotation();
    ServerRotationRestore restore{player,previous};
    const_cast<Vec2&>(player.getRotation())={request->pitch,request->yaw};
    auto const result=origin(player,isSenderAuthority); // Never retry native origin.
    if(receiverTraceCount.fetch_add(1,std::memory_order_relaxed)<128) {
        if(auto* f=std::fopen("./mods/LHolo/flexible-placement-trace.log","ab")) {
            std::fprintf(f,"%llu local-receiver previousYaw=%.2f placementYaw=%.2f authority=%d result=%d cell=%d,%d,%d\n",
                GetTickCount64(),previous.y,request->yaw,isSenderAuthority,static_cast<int>(result),
                request->destination.x,request->destination.y,request->destination.z);
            try {
                auto const state=player.getDimensionBlockSource().getBlock(request->destination).toDebugString();
                std::fprintf(f,"  authoritative-block=%s\n",state.c_str());
            } catch (...) {}
            std::fclose(f);
        }
    }
    return result;
}
}

bool rememberLocalPlacementRotation(LocalPlayer const& player,BlockPos const& destination,
    ItemUseInventoryTransaction const& tx,float pitch,float yaw,
    std::uint64_t ownerToken,std::uint64_t ownerEpoch) noexcept {
    try {
        if(!receiverAvailable.load(std::memory_order_acquire) || !std::isfinite(pitch)
            || !std::isfinite(yaw) || std::fabs(pitch)>90.F || !ownerToken || !ownerEpoch) return false;
        auto const slot=player.getSelectedItemSlot();
        if(slot<0 || slot>=9 || tx.mSlot!=slot) return false;
        auto const& item=player.getInventory().getItem(slot);
        if(item.isNull()) return false;
        auto const now=GetTickCount64();
        std::lock_guard lock(receiptMutex);
        for(auto& entry:receipts) {
            if(entry && entry->expires<=now) entry.reset();
            if(entry) continue;
            entry=LocalReceipt{static_cast<std::uint64_t>(player.getRuntimeID()),now+250,
                ownerToken,ownerEpoch,&player,tx.mPos.get(),destination,tx.mTargetBlockId,
                static_cast<int>(player.getDimensionId()),slot,item.getId(),item.getIdAux(),item.mCount,
                tx.mFace,tx.mClickPos.get(),tx.mFromPos.get(),pitch,yaw};
            return true;
        }
    } catch (...) {}
    return false;
}

void cancelLocalPlacementRotations(std::uint64_t ownerToken,std::uint64_t ownerEpoch) noexcept {
    std::lock_guard lock(receiptMutex);
    for(auto& entry:receipts)
        if(entry && (ownerToken==0 || (entry->ownerToken==ownerToken && entry->ownerEpoch==ownerEpoch)))
            entry.reset();
}

bool installLocalPlacementRotationHook() {
    auto const ok=LocalPlacementUseRotationHook::hook()==0;
    receiverAvailable.store(ok,std::memory_order_release);
    return ok;
}
bool uninstallLocalPlacementRotationHook() {
    receiverAvailable.store(false,std::memory_order_release);
    cancelLocalPlacementRotations();
    return LocalPlacementUseRotationHook::unhook();
}
}
