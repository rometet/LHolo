#include "place/PlacementRotationDelivery.h"
#include "place/ExternalRotationDeliveryPolicy.h"
#include "place/LocalPlacementRotation.h"
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
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <optional>
#include <thread>

namespace lholo::place::detail {
namespace {
// Bounded per-session diagnostic. Never runs on the inactive frame path.
void traceFlexibleDelivery(char const* format, ...) noexcept {
    static std::atomic_uint records{};
    if (records.fetch_add(1,std::memory_order_relaxed) >= 512) return;
    auto* file = std::fopen("./mods/LHolo/flexible-placement-trace.log", "ab");
    if (!file) return;
    std::fprintf(file,"%llu ",GetTickCount64());
    va_list args; va_start(args,format); std::vfprintf(file,format,args); va_end(args);
    std::fputc('\n',file); std::fclose(file);
}
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
    std::uint64_t ownerToken{};
    std::uint64_t ownerEpoch{};
    std::uint64_t worldGeneration{};
    int dimension{};
    std::uint32_t flags{};
    float maxPickRange{};
    bool airTarget{};
    ExternalRotationWarmup warmup{};
    bool strictStair{};
};
std::mutex pendingMutex;
std::optional<PendingRotation> pending;
std::atomic_bool available{};

struct ScopedExternalInputRotation {
    PlayerAuthInputPacket& input;
    Vec2 rot, interact, move, analog, raw;
    Vec3 camera;
    float head;
    ScopedExternalInputRotation(PlayerAuthInputPacket& p,float pitch,float yaw)
        : input(p),rot(p.mRot.get()),interact(p.mInteractRotation.get()),
          move(p.mMove.get()),analog(p.mAnalogMoveVector.get()),raw(p.mRawMoveVector.get()),
          camera(p.mCameraOrientation.get()),head(p.mYHeadRot) {
        auto remap=[&](Vec2 v) {
            auto r=preserveWorldMove(v.x,v.y,rot.y,yaw);return Vec2{r.x,r.y};
        };
        input.mMove.get()=remap(move);input.mAnalogMoveVector.get()=remap(analog);
        input.mRawMoveVector.get()=remap(raw);
        input.mRot.get()={pitch,yaw};input.mInteractRotation.get()={pitch,yaw};
        input.mYHeadRot=yaw;
        float const pRad=pitch*0.01745329251994329577F,yRad=yaw*0.01745329251994329577F;
        input.mCameraOrientation.get()={-std::sin(yRad)*std::cos(pRad),-std::sin(pRad),std::cos(yRad)*std::cos(pRad)};
    }
    ~ScopedExternalInputRotation() {
        input.mRot.get()=rot;input.mInteractRotation.get()=interact;input.mYHeadRot=head;
        input.mCameraOrientation.get()=camera;input.mMove.get()=move;
        input.mAnalogMoveVector.get()=analog;input.mRawMoveVector.get()=raw;
    }
    ScopedExternalInputRotation(ScopedExternalInputRotation const&)=delete;
};

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

struct PraxisModuleLease {
    HMODULE module{};
    PraxisModuleLease() noexcept {
        if (!GetModuleHandleExW(0, L"PraxisCompanion.dll", &module))
            GetModuleHandleExW(0, L"praxiscompanion.dll", &module);
    }
    ~PraxisModuleLease() { if (module) FreeLibrary(module); }
    PraxisModuleLease(PraxisModuleLease const&) = delete;
    PraxisModuleLease& operator=(PraxisModuleLease const&) = delete;
};

using PraxisOwnerStatusV2Fn =
    lholo::flexible_placement_abi::FlexiblePlacementOwnerStatusV2(__cdecl*)(
        lholo::flexible_placement_abi::FlexiblePlacementOwnerQueryV2);

bool praxisOwnerStillLive(PendingRotation const& p) {
    PraxisModuleLease lease;
    if (!lease.module) return false;
    auto const status = reinterpret_cast<PraxisOwnerStatusV2Fn>(GetProcAddress(
        lease.module, "praxis_flexible_placement_v2_get_owner_status"));
    if (!status) return false;
    using namespace lholo::flexible_placement_abi;
    FlexiblePlacementOwnerQueryV2 query{};
    query.flags = p.flags;
    query.ownerToken = p.ownerToken;
    query.ownerEpoch = p.ownerEpoch;
    query.worldGeneration = p.worldGeneration;
    query.dimension = p.dimension;
    query.selectedSlot = p.slot;
    query.itemId = p.item.getId();
    query.itemAux = p.item.getIdAux();
    query.itemCount = p.item.mCount;
    auto const live = status(query);
    return live.version == kVersion2 && (live.flags & OwnerLive) != 0
        && live.ownerToken == p.ownerToken && live.ownerEpoch == p.ownerEpoch
        && live.worldGeneration == p.worldGeneration && live.dimension == p.dimension
        && live.selectedSlot == p.slot && live.itemId == p.item.getId()
        && live.itemAux == p.item.getIdAux()
        && live.itemCount == static_cast<std::uint32_t>(p.item.mCount);
}

// Strict stair uses MUST be attached to a genuine native AuthInput that
// already carries the same viewing direction. Never rewrite player/movement
// rotation or accept the local planner's yaw without this same-tick proof.
bool stillValid(PendingRotation const& p,ClientInstance& client) {
    auto& state=PlacementState::getInstance();
    if (GetTickCount64()>=p.expires || p.epoch!=state.manualInputEpoch()
        || client.getLocalPlayer()!=p.player || !client.isInGameInputEnabled()
        || structure::isGuiVisible()) return false;
    if (p.external) {
        // Praxis owns this one request only while its exact token/epoch is
        // still live. Mode transitions, focus loss, GUI entry, world reset,
        // and feature disable cancel it through the v2 contract; recheck the
        // LHolo side here as the final pre-serialization owner gate too.
        if (!p.ownerToken || !p.ownerEpoch || state.enabled() || state.manualMode()
            || state.rangeEnabled() || p.dimension != static_cast<int>(p.player->getDimensionId())) return false;
        if (!praxisOwnerStillLive(p)) return false;
    } else if (!state.enabled() && !state.manualMode() && !state.rangeEnabled()) return false;
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
        auto const& destination = region.getBlock(p.cell);
        if (!destination.isAir()) return false;
        auto const local = tx->mClickPos.get();
        auto const eye = p.player->getEyePos();
        auto const worldHit = Vec3{static_cast<float>(support.x) + local.x,
            static_cast<float>(support.y) + local.y, static_cast<float>(support.z) + local.z};
        auto const dx = worldHit.x - eye.x, dy = worldHit.y - eye.y, dz = worldHit.z - eye.z;
        auto const pickRange = p.player->getPickRange();
        if (!std::isfinite(pickRange) || pickRange <= 0.0F || !std::isfinite(p.maxPickRange)
            || p.maxPickRange <= 0.0F || p.maxPickRange > pickRange + 0.001F
            || !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)
            || dx * dx + dy * dy + dz * dz > pickRange * pickRange) return false;
        if (p.airTarget) {
            if (support != p.cell || tx->mTargetBlockId != p.targetBlockId
                || region.getBlock(p.cell).mNetworkId != p.targetBlockId) return false;
        } else if (region.getBlock(p.support).isAir()
            || region.getBlock(p.support).mNetworkId != p.targetBlockId) return false;
        return support == p.support
            && transactionFace(*tx) == p.face
            && tx->mTargetBlockId == p.targetBlockId;
    }
    auto const ghost=projection::queryPlacementProjection(*p.player,p.cell);
    return ghost.block && ghost.missing && ghost.block->mNetworkId==p.ghostId
        && region.getBlock(transactionSupport(*tx)).mNetworkId==tx->mTargetBlockId;
}

LL_TYPE_INSTANCE_HOOK(RotationAuthInputHook,ll::memory::HookPriority::Normal,
    LoopbackPacketSender,&LoopbackPacketSender::$sendToServer,void,Packet& packet) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard || !available.load(std::memory_order_acquire)) { origin(packet); return; }
    {
        std::lock_guard lock(pendingMutex);
        if (pending && pending->external) {
            static std::uint64_t tracedExpires{};
            static unsigned observed{};
            if (tracedExpires != pending->expires) { tracedExpires=pending->expires; observed=0; }
            if (observed++ < 4)
                traceFlexibleDelivery("packet id=%u external=1 threadMatch=%d expiresIn=%lld", unsigned(packet.getId()),
                    pending->thread==std::this_thread::get_id(),
                    static_cast<long long>(pending->expires)-static_cast<long long>(GetTickCount64()));
        }
    }
    if (packet.getId()!=MinecraftPacketIds::PlayerAuthInputPacket) { origin(packet);return; }
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
        || input.mInputData.get().contains(Input::PerformBlockActions)) {
        { std::lock_guard lock(pendingMutex); if (pending && pending->requireNativeRotationSync) pending->warmup.reset(); }
        origin(packet);return;
    }
    std::optional<PendingRotation> request;
    {
        std::lock_guard lock(pendingMutex);
        if (pending && pending->thread==std::this_thread::get_id()) {
            request=std::move(pending);pending.reset();
        }
    }
    if (!request) { origin(packet);return; }
    // Direction-sensitive placements cannot steal a native item/block action.
    // The guard above checks packet ownership; when a different facing is
    // required, native input ticks first synchronize server rotation. A
    // scoped packet override preserves world movement and restores all fields
    // after forwarding without changing the player's rendered local camera.
    bool valid=false;
    app::invokeNativeCallback([&] { valid=stillValid(*request,*client); },
        [](char const* reason) noexcept { app::reportNativeCallbackFailure("rotation placement validation",reason); });
    if (request->external)
        traceFlexibleDelivery("validate=%d face=%u requestedYaw=%.2f nativeYaw=%.2f cell=%d,%d,%d",
            valid,unsigned(request->face),request->yaw,input.mRot.get().y,
            request->cell.x,request->cell.y,request->cell.z);
    if (!valid) { origin(packet);return; }
    // Both Praxis FlexiblePlacement and LHolo EasyPlace/Manual/Range use the
    // same server-authoritative rotation protocol. The first two native input
    // ticks establish a different facing before a real item-use is attached.
    // Already-aligned LHolo placements use the current native tick directly.
    // A just-turned camera can already match the desired yaw on THIS native
    // packet while the server still has the previous yaw. Always pre-roll
    // stairs, including when the local packet's rotations appear aligned.
    bool const needsWarmup=request->strictStair || needsOrientationWarmup(request->external,
        request->requireNativeRotationSync,input.mRot.get().x,input.mRot.get().y,
        request->pitch,request->yaw);
    std::optional<ScopedExternalInputRotation> externalRotation;
    if (request->requireNativeRotationSync) {
        auto const tick=static_cast<std::uint64_t>(input.mClientTick.get().mValue);
        bool const placeNow=!needsWarmup || request->warmup.ready(tick);
        externalRotation.emplace(input,request->pitch,request->yaw);
        if (!placeNow) {
            auto const forwarded=app::invokeNativeCallback([&] { origin(packet); },
                [](char const* reason) noexcept { app::reportNativeCallbackFailure("flexible rotation pre-roll",reason); });
            if (forwarded) {
                request->warmup.forwardedPrime(tick);
                traceFlexibleDelivery("BDS_PRIME tick=%llu primes=%u yaw=%.2f head=%.2f no-placement=1",
                    static_cast<unsigned long long>(tick),request->warmup.primes,request->yaw,float(input.mYHeadRot));
                std::lock_guard lock(pendingMutex);
                if (!pending && GetTickCount64()<request->expires
                    && request->epoch==PlacementState::getInstance().manualInputEpoch()) pending=std::move(request);
            }
            return;
        }
        traceFlexibleDelivery("BDS_PLACE tick=%llu primes=%u yaw=%.2f head=%.2f",
            static_cast<unsigned long long>(tick),request->warmup.primes,request->yaw,float(input.mYHeadRot));
    }
    auto const savedRotation=input.mInteractRotation.get();
    auto const savedPlayerRotation=input.mRot.get();
    auto const savedFlags=input.mInputData.get();
    bool attached=false;
    app::invokeNativeCallback([&] {
        auto& payload=*request->payload;
        auto const* tx=std::get_if<ItemUseInventoryTransaction>(&payload.mVariantTransaction.get());
        if (!tx) return;
        // LHolo's projected placements have an exact, native-predicted target
        // state. Singleplayer's integrated server may still read ServerPlayer
        // facing rather than the packet fields, so borrow the verified facing
        // for this exact transaction (never a raw block-state rewrite).
        if (!request->external && request->requireNativeRotationSync) {
            rememberLocalPlacementRotation(*request->player,request->cell,*tx,
                request->pitch,request->yaw,0x4C486F6C6F455059ULL,
                request->epoch+1U);
        }
        if (request->external && request->requireNativeRotationSync)
            traceFlexibleDelivery("BDS_PROTOCOL_ONLY local-receiver-override=0 primes=%u",request->warmup.primes);
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
    if (request->external)
        traceFlexibleDelivery("forward attached=%d packetYaw=%.2f interactYaw=%.2f localYaw=%.2f",
            attached,input.mRot.get().y,input.mInteractRotation.get().y,request->player->getRotation().y);
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
    // v1 has no owner token, focus epoch, world generation, or dispatch
    // cancellation. Keep the symbol for old binaries, but fail closed.
    if (request.version != lholo::flexible_placement_abi::kVersion || request.reserved != 0)
        return lholo::flexible_placement_abi::QueueRejected;
    return lholo::flexible_placement_abi::QueueUnavailable;
}

extern "C" __declspec(dllexport)
lholo::flexible_placement_abi::FlexiblePlacementStatusV2
lholo_flexible_placement_v2_get_status() noexcept {
    using namespace lholo::flexible_placement_abi;
    FlexiblePlacementStatusV2 result{};
    try {
        if (!available.load(std::memory_order_acquire)) return result;
        auto const& state = PlacementState::getInstance();
        if (state.enabled()) result.statusBits |= EasyPlaceOwnsPlacement;
        if (state.manualMode()) result.statusBits |= ManualOwnsPlacement;
        if (state.rangeEnabled()) result.statusBits |= RangeOwnsPlacement;
        std::lock_guard lock(pendingMutex);
        if (pending && pending->external
            && (GetTickCount64() >= pending->expires || pending->epoch != state.manualInputEpoch())) {
            traceFlexibleDelivery("retire-expired pending request (was falsely reported busy)");
            pending.reset();
        }
        if (pending && pending->external) {
            result.ownerToken = pending->ownerToken;
            result.ownerEpoch = pending->ownerEpoch;
            result.worldGeneration = pending->worldGeneration;
            result.dimension = pending->dimension;
        }
    } catch (...) {
        result = {};
    }
    return result;
}

extern "C" __declspec(dllexport)
std::uint32_t lholo_flexible_placement_v2_queue(
    lholo::flexible_placement_abi::FlexiblePlacementRequestV2 request) noexcept {
    using namespace lholo::flexible_placement_abi;
    constexpr auto knownFlags = RequestGameplay | RequestFocused | RequestGuiClear
        | RequestModifiers | RequestAirTarget | RequestRotationSync
        | RequestRotationMode | RequestOffsetMode | RequestAdjacentMode;
    constexpr auto modeFlags = RequestRotationMode | RequestOffsetMode | RequestAdjacentMode;
    try {
        if (request.version != kVersion2 || request.reserved != 0 || request.reserved2 != 0
            || (request.flags & ~knownFlags) != 0
            || (request.flags & (RequestGameplay | RequestFocused | RequestGuiClear | RequestModifiers))
                != (RequestGameplay | RequestFocused | RequestGuiClear | RequestModifiers)
            || (request.flags & modeFlags) == 0
            || request.ownerToken == 0 || request.ownerEpoch == 0 || request.face >= 6
            || request.selectedSlot < 0 || request.selectedSlot >= 9
            || !std::isfinite(request.relativeHitX) || !std::isfinite(request.relativeHitY)
            || !std::isfinite(request.relativeHitZ) || !std::isfinite(request.pitch)
            || !std::isfinite(request.yaw) || !std::isfinite(request.maxPickRange)
            || std::fabs(request.pitch) > 90.F || request.maxPickRange <= 0.0F) return QueueRejected;
        if (lholo_flexible_placement_v1_get_status() != 0) return QueueBusy;
        auto client = ll::service::getClientInstance();
        if (!client || !client->isInGameInputEnabled() || structure::isGuiVisible()) return QueueRejected;
        auto* player = client->getLocalPlayer();
        if (!player || static_cast<int>(player->getDimensionId()) != request.dimension) return QueueRejected;
        auto const pickRange = player->getPickRange();
        if (!std::isfinite(pickRange) || pickRange <= 0.0F || request.maxPickRange > pickRange + 0.001F)
            return QueueRejected;
        auto const& item = player->getInventory().getItem(request.selectedSlot);
        if (item.isNull() || item.mCount == 0 || !item.mBlock || item.getId() != request.itemId
            || item.getIdAux() != request.itemAux
            || static_cast<std::uint32_t>(item.mCount) != request.itemCount) return QueueRejected;
        auto const inRange = [](float value) { return value >= 0.0F && value <= 1.0F; };
        auto const on = [](float value, float boundary) { return std::fabs(value - boundary) <= 0.0001F; };
        auto const local = Vec3{request.relativeHitX, request.relativeHitY, request.relativeHitZ};
        if (!inRange(local.x) || !inRange(local.y) || !inRange(local.z)) return QueueRejected;
        if ((request.face == 0 && !on(local.y, 0.0F)) || (request.face == 1 && !on(local.y, 1.0F))
            || (request.face == 2 && !on(local.z, 0.0F)) || (request.face == 3 && !on(local.z, 1.0F))
            || (request.face == 4 && !on(local.x, 0.0F)) || (request.face == 5 && !on(local.x, 1.0F)))
            return QueueRejected;
        auto const inWorld = [](std::int64_t x, std::int64_t y, std::int64_t z) {
            return x >= -30000000LL && x <= 30000000LL && z >= -30000000LL && z <= 30000000LL
                && y >= -2048LL && y <= 4096LL;
        };
        auto const sx = static_cast<std::int64_t>(request.supportX);
        auto const sy = static_cast<std::int64_t>(request.supportY);
        auto const sz = static_cast<std::int64_t>(request.supportZ);
        bool const airTarget = (request.flags & RequestAirTarget) != 0;
        auto const dx = airTarget ? sx : sx + (request.face == 5 ? 1 : request.face == 4 ? -1 : 0);
        auto const dy = airTarget ? sy : sy + (request.face == 1 ? 1 : request.face == 0 ? -1 : 0);
        auto const dz = airTarget ? sz : sz + (request.face == 3 ? 1 : request.face == 2 ? -1 : 0);
        if (!inWorld(sx, sy, sz) || !inWorld(dx, dy, dz)) return QueueRejected;
        BlockPos const support{request.supportX, request.supportY, request.supportZ};
        BlockPos const destination{static_cast<int>(dx), static_cast<int>(dy), static_cast<int>(dz)};
        auto& region = player->getDimensionBlockSource();
        if (!region.getBlock(destination).isAir()) return QueueRejected;
        if (airTarget) {
            if (support != destination
                || region.getBlock(destination).mNetworkId != request.targetBlockId
                || !item.mBlock->getBlockType().mIsOpaqueFullBlock
                || item.mBlock->getBlockType().isSlabBlock()) return QueueRejected;
        } else if (region.getBlock(support).isAir()
            || region.getBlock(support).mNetworkId != request.targetBlockId) return QueueRejected;
        auto const eye = player->getEyePos();
        Vec3 const worldHit{static_cast<float>(request.supportX) + local.x,
            static_cast<float>(request.supportY) + local.y, static_cast<float>(request.supportZ) + local.z};
        auto const vx = worldHit.x - eye.x, vy = worldHit.y - eye.y, vz = worldHit.z - eye.z;
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)
            || vx * vx + vy * vy + vz * vz > pickRange * pickRange) return QueueRejected;
        auto transactionBase = ComplexInventoryTransaction::fromType(
            ComplexInventoryTransaction::Type::ItemUseTransaction, InventoryTransaction{}
        );
        if (!transactionBase) return QueueRejected;
        auto& transaction = static_cast<ItemUseInventoryTransaction&>(*transactionBase);
        transaction.mType = ComplexInventoryTransaction::Type::ItemUseTransaction;
        transaction.mActionType = ItemUseInventoryTransaction::ActionType::Place;
        transaction.mTriggerType = ItemUseInventoryTransaction::TriggerType::PlayerInput;
        transaction.mPos = support;
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
        return lholo::place::detail::queueExternalRotationPlacement(
            *player,destination,support,request.targetBlockId,static_cast<uchar>(request.face),
            request.selectedSlot,item,request.pitch,request.yaw,std::move(transactionBase),
            request.ownerToken,request.ownerEpoch,request.worldGeneration,request.dimension,
            request.flags,request.maxPickRange)
            ? QueueAccepted : QueueRejected;
    } catch (...) { return QueueRejected; }
}

extern "C" __declspec(dllexport)
std::uint32_t lholo_flexible_placement_v2_cancel(
    std::uint64_t ownerToken, std::uint64_t ownerEpoch) noexcept {
    try {
        if (!available.load(std::memory_order_acquire)) return lholo::flexible_placement_abi::QueueUnavailable;
        return lholo::place::detail::cancelExternalRotationPlacement(ownerToken, ownerEpoch)
            ? lholo::flexible_placement_abi::QueueAccepted
            : lholo::flexible_placement_abi::QueueRejected;
    } catch (...) {
        return lholo::flexible_placement_abi::QueueRejected;
    }
}

bool queueRotationPlacement(LocalPlayer& player,BlockPos const& cell,std::int64_t cellKey,
    unsigned ghostId,int slot,ItemStack const& item,float pitch,float yaw,
    std::unique_ptr<ComplexInventoryTransaction> tx,
    bool requireNativeRotationSync,bool requireStairWarmup) {
    // Both angles must be validated before borrowing any native packet state.
    // Pitch is the Bedrock interaction pitch in [-90,90]; do not set camera,
    // body/head yaw or invent a second network tick.
    if (!available.load(std::memory_order_acquire) || !tx
        || !std::isfinite(pitch) || !std::isfinite(yaw)
        || std::fabs(pitch)>90.F || slot<0 || slot>=9) return false;
    std::lock_guard lock(pendingMutex);
    auto& state=PlacementState::getInstance();auto const now=GetTickCount64();
    if (pending && pending->expires <= now) pending.reset();
    if (pending && pending->external) return false;
    // An existing pending AuthInput placement owns this tick; a second
    // legacy placement would change the selected stack and invalidate it.
    if (pending && pending->expires>now && pending->epoch==state.manualInputEpoch()) return true;
    pending.reset();
    auto payload=std::make_unique<InventoryTransactionPacketPayload>(std::move(tx),true);
    pending.emplace(PendingRotation{&player,std::this_thread::get_id(),state.manualInputEpoch(),now+500,
        cell,cellKey,ghostId,slot,item,pitch,yaw,requireNativeRotationSync,
        std::move(payload)});
    pending->strictStair=requireStairWarmup && requireNativeRotationSync;
    return true;
}

bool queueExternalRotationPlacement(LocalPlayer& player, BlockPos const& cell,
    BlockPos const& support, std::uint32_t targetBlockId, uchar face, int slot,
    ItemStack const& item, float pitch, float yaw,
    std::unique_ptr<ComplexInventoryTransaction> tx, std::uint64_t ownerToken,
    std::uint64_t ownerEpoch, std::uint64_t worldGeneration, int dimension,
    std::uint32_t flags, float maxPickRange) {
    if (!available.load(std::memory_order_acquire) || !tx
        || face >= 6 || slot < 0 || slot >= 9
        || !std::isfinite(pitch) || !std::isfinite(yaw)
        || std::fabs(pitch) > 90.F || !ownerToken || !ownerEpoch
        || !std::isfinite(maxPickRange) || maxPickRange <= 0.0F) return false;
    auto client = ll::service::getClientInstance();
    if (!client || client->getLocalPlayer() != &player
        || !client->isInGameInputEnabled() || structure::isGuiVisible()
        || player.getSelectedItemSlot() != slot
        || static_cast<int>(player.getDimensionId()) != dimension) return false;
    auto const& current = player.getInventory().getItem(slot);
    if (current.isNull() || !current.matchesNetIdVariant(item)) return false;
    auto& region = player.getDimensionBlockSource();
    bool const airTarget = (flags & lholo::flexible_placement_abi::RequestAirTarget) != 0;
    if (!region.getBlock(cell).isAir()) return false;
    if (airTarget) {
        if (support != cell || region.getBlock(cell).mNetworkId != targetBlockId) return false;
    } else if (region.getBlock(support).isAir()
        || region.getBlock(support).mNetworkId != targetBlockId) return false;
    BlockPos const expected{
        support.x + (face == 5 ? 1 : face == 4 ? -1 : 0),
        support.y + (face == 1 ? 1 : face == 0 ? -1 : 0),
        support.z + (face == 3 ? 1 : face == 2 ? -1 : 0),
    };
    if ((!airTarget && expected != cell)
        || (airTarget && support != cell)) return false;

    std::lock_guard lock(pendingMutex);
    auto& state=PlacementState::getInstance();
    if (state.enabled() || state.manualMode() || state.rangeEnabled()) return false;
    auto const now=GetTickCount64();
    if (pending && pending->expires <= now) pending.reset();
    if (pending && pending->external) return false;
    if (pending && pending->expires>now && pending->epoch==state.manualInputEpoch()) return false;
    pending.reset();
    auto payload=std::make_unique<InventoryTransactionPacketPayload>(std::move(tx),true);
    pending.emplace(PendingRotation{&player,std::this_thread::get_id(),state.manualInputEpoch(),now+500,
        cell,bridgeCellKey(cell),targetBlockId,slot,item,pitch,yaw,
        (flags & lholo::flexible_placement_abi::RequestRotationSync) != 0,std::move(payload),
        true,support,targetBlockId,face,ownerToken,ownerEpoch,worldGeneration,dimension,flags,
        maxPickRange,airTarget});
    traceFlexibleDelivery("queue face=%u yaw=%.2f pitch=%.2f cell=%d,%d,%d rotSync=%d",
        unsigned(face),yaw,pitch,cell.x,cell.y,cell.z,
        (flags & lholo::flexible_placement_abi::RequestRotationSync)!=0);
    return true;
}

bool cancelExternalRotationPlacement(std::uint64_t ownerToken,std::uint64_t ownerEpoch) noexcept {
    cancelLocalPlacementRotations(ownerToken,ownerEpoch);
    std::lock_guard lock(pendingMutex);
    if (!pending || !pending->external || pending->ownerToken != ownerToken
        || pending->ownerEpoch != ownerEpoch) return false;
    pending.reset();
    return true;
}

void cancelAllExternalRotationPlacements() noexcept {
    cancelLocalPlacementRotations();
    std::lock_guard lock(pendingMutex);
    if (pending && pending->external) pending.reset();
}

bool installRotationDeliveryHook() {
    if (!installLocalPlacementRotationHook()) return false;
    auto const ok=RotationAuthInputHook::hook()==0;
    if (!ok) uninstallLocalPlacementRotationHook();
    available.store(ok,std::memory_order_release);return ok;
}
bool uninstallRotationDeliveryHook() {
    available.store(false,std::memory_order_release);
    // Whole-hook shutdown must release every native-owned payload. The
    // selective external cancellation API above is the only path that leaves
    // LHolo's own pending request untouched.
    { std::lock_guard lock(pendingMutex); pending.reset(); }
    auto const sendRemoved=RotationAuthInputHook::unhook();
    auto const receiveRemoved=uninstallLocalPlacementRotationHook();
    return sendRemoved && receiveRemoved;
}
}
