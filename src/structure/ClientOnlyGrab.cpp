// Client-only, experimental server-authoritative material grabbing.
// Unlike the old hidden native chest, the client does not install a UIScene.
// It accepts the server's real ContainerOpen/InventoryContent then dispatches
// one normal ItemStackRequest. No server behavior pack or custom command.
#include "structure/ClientOnlyGrab.h"
#include "structure/MaterialGrabRules.h"
#include "structure/InventoryContents.h"
#include "structure/StructureUiState.h"
#include "structure/StructureSession.h"
#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "plugin/LHolo.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/network/LegacyClientNetworkHandler.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/PacketSender.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/ContainerOpenPacket.h"
#include "mc/network/packet/ContainerClosePacket.h"
#include "mc/network/packet/InventoryContentPacket.h"
#include "mc/network/packet/InventorySlotPacket.h"
#include "mc/network/packet/ItemStackRequestPacket.h"
#include "mc/network/packet/ItemStackResponseSlotInfo.h"
#include "mc/network/packet/ItemStackResponseContainerInfo.h"
#include "mc/network/packet/ItemStackResponseInfo.h"
#include "mc/network/packet/ItemStackResponsePacket.h"
#include "mc/network/packet/item_stack_request_packet_data/RequestData.h"
#include "mc/network/packet/cerealize/types/item_stack_request_cereal/TakeActionData.h"
#include "mc/world/inventory/network/ItemStackNetManagerClient.h"
#include "mc/world/inventory/network/ItemStackNetIdVariant.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/containers/FullContainerName.h"
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <variant>

namespace lholo::structure::detail {
namespace {
constexpr std::uint64_t kOpenTimeout=900, kContentTimeout=1600, kRequestTimeout=2200;
std::atomic_bool installed{false};
struct Slot {
    std::string id;
    int amount{};
    ItemStackNetIdVariant netId;
    int index{};
};
struct SilentSession {
    enum class Phase {Idle, Armed, Open, Ready, Waiting, Closing};
    Phase phase{Phase::Idle};
    std::uint64_t begun{},lastSent{};
    ContainerID containerId{ContainerID::None};
    SharedTypes::Legacy::ContainerType type{SharedTypes::Legacy::ContainerType::Container};
    FullContainerName sourceName{};
    ContainerEnumName expectedSource{ContainerEnumName::LevelEntityContainer};
    std::vector<Slot> slots;
    std::optional<ItemStackRequestId> requestId;
    int beforeInventoryCount{};
    int attempt{};
    std::string expectedItem;
};
std::mutex mutex;
SilentSession sess;

bool enabled() noexcept {
    constexpr auto file=L".\\mods\\LHolo\\config\\client_only_grab.enabled";
    auto const flags=GetFileAttributesW(file);
    return flags!=INVALID_FILE_ATTRIBUTES && !(flags&FILE_ATTRIBUTE_DIRECTORY);
}
bool alt() noexcept {return !!(GetAsyncKeyState(VK_MENU)&0x8000);}
bool validContainerBlock(Block const* block) {
    if(!block)return false;
    auto const id=block->getTypeName();
    return id=="minecraft:chest" || id=="minecraft:trapped_chest"
        || id=="minecraft:barrel" || id=="minecraft:shulker_box"
        || (id.starts_with("minecraft:") && id.ends_with("_shulker_box"));
}
void log(char const* event,int a=0,int b=0) {
    LHolo::getInstance().getSelf().getLogger().info(
        "MATERIAL_GRAB_CLIENT_ONLY {} a={} b={}",event,a,b);
}
void closeSession() {
    ContainerID containerId{ContainerID::None};
    SharedTypes::Legacy::ContainerType type{SharedTypes::Legacy::ContainerType::Container};
    {
        std::lock_guard lock(mutex);
        if(sess.phase==SilentSession::Phase::Idle)return;
        containerId=sess.containerId;
        type=sess.type;
        sess={};
    }
    if(containerId==ContainerID::None)return; // No server session to close.
    auto client=ll::service::getClientInstance();
    if(!client || !client->getLocalPlayer())return;
    auto packet=MinecraftPackets::createPacket(MinecraftPacketIds::ContainerClose);
    if(!packet)return;
    auto& close=static_cast<ContainerClosePacket&>(*packet);
    close.mContainerId=containerId;
    close.mContainerType=type;
    close.mServerInitiatedClose=false;
    client->getPacketSender().sendToServer(*packet);
    log("CLOSED");
}
// Diagnostic comparison with Mojang's normal quick-move requests.
// Observe only, never mutate/rewrite packets. The local integrated world uses
// LoopbackPacketSender; other transport implementations are untouched.
std::atomic_bool requestTraceInstalled{};
LL_TYPE_INSTANCE_HOOK(SilentRequestTraceHook,ll::memory::HookPriority::Low,
    LoopbackPacketSender,&LoopbackPacketSender::$sendToServer,void,Packet& packet) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && packet.getId()==MinecraftPacketIds::ItemStackRequest) {
        static std::atomic<int> captured{};
        if(captured.fetch_add(1,std::memory_order_relaxed)<24) {
            auto& req=static_cast<ItemStackRequestPacket&>(packet);
            for(auto const& entry:*req.mRequests) {
                LHolo::getInstance().getSelf().getLogger().info(
                    "MATERIAL_GRAB_CLIENT_ONLY_PACKET_TRACE request={} actions={}",
                    entry.mClientRequestId->mRawId,entry.mActions->size());
                for(auto const& action:*entry.mActions) {
                    if(auto const* take=std::get_if<ItemStackRequestCereal::TakeActionData>(&action)) {
                        LHolo::getInstance().getSelf().getLogger().info(
                            "MATERIAL_GRAB_CLIENT_ONLY_PACKET_TAKE src={} slot={} net={} dst={} slot={} net={} amount={}",
                            take->mSource->mFullContainerName->toString(),
                            static_cast<int>(take->mSource->mSlot),
                            take->mSource->mNetIdVariant->toString(),
                            take->mDestination->mFullContainerName->toString(),
                            static_cast<int>(take->mDestination->mSlot),
                            take->mDestination->mNetIdVariant->toString(),
                            static_cast<int>(take->mAmount));
                    } else if(auto const* place=std::get_if<ItemStackRequestCereal::PlaceActionData>(&action)) {
                        LHolo::getInstance().getSelf().getLogger().info(
                            "MATERIAL_GRAB_CLIENT_ONLY_PACKET_PLACE src={} slot={} net={} dst={} slot={} net={} amount={}",
                            place->mSource->mFullContainerName->toString(),
                            static_cast<int>(place->mSource->mSlot),
                            place->mSource->mNetIdVariant->toString(),
                            place->mDestination->mFullContainerName->toString(),
                            static_cast<int>(place->mDestination->mSlot),
                            place->mDestination->mNetIdVariant->toString(),
                            static_cast<int>(place->mAmount));
                    } else {
                        LHolo::getInstance().getSelf().getLogger().info(
                            "MATERIAL_GRAB_CLIENT_ONLY_PACKET_OTHER_ACTION type={}",action.index());
                    }
                }
            }
        }
    }
    origin(packet);
}

// Only use a server-named, successfully opened container. We do not accept
// inventory contents before a matching container-open handshake.
LL_TYPE_INSTANCE_HOOK(SilentContainerOpenHook,ll::memory::HookPriority::Normal,
    LegacyClientNetworkHandler,&LegacyClientNetworkHandler::$handle,void,
    NetworkIdentifier const& source,ContainerOpenPacket const& packet) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && installed.load(std::memory_order_acquire) && enabled()) {
        std::lock_guard lock(mutex);
        if(enabled() && alt()) {
            static std::atomic<int> diagnosticCount{};
            if(diagnosticCount.fetch_add(1,std::memory_order_relaxed)<12)
                LHolo::getInstance().getSelf().getLogger().info(
                    "MATERIAL_GRAB_CLIENT_ONLY_OPEN_TRACE phase={} age_ms={}",
                    static_cast<int>(sess.phase),GetTickCount64()-sess.begun);
        }
        if(sess.phase==SilentSession::Phase::Armed
            && GetTickCount64()-sess.begun<kOpenTimeout
            && packet.mType==SharedTypes::Legacy::ContainerType::Container) {
            sess.phase=SilentSession::Phase::Open;
            sess.containerId=packet.mContainerId;
            sess.type=packet.mType;
            log("SERVER_CONTAINER_OPEN",static_cast<int>(packet.mContainerId));
            return; // No UIScene -> HUD and camera stay in the world.
        }
    }
    origin(source,packet);
}
LL_TYPE_INSTANCE_HOOK(SilentInventoryContentHook,ll::memory::HookPriority::Normal,
    LegacyClientNetworkHandler,&LegacyClientNetworkHandler::$handle,void,
    NetworkIdentifier const& source,InventoryContentPacket const& packet) {
    app::hook_lifecycle::DetourGuard guard;
    bool consumed=false;
    if(guard && installed.load(std::memory_order_acquire)) {
        std::lock_guard lock(mutex);
        if(sess.phase==SilentSession::Phase::Open
            && sess.containerId==packet.mInventoryId) {
            sess.slots.clear();
            sess.sourceName=packet.mFullContainerName;
            // InventoryContentPacket does not carry the request collection
            // name reliably (chest observed as anvil_input_items). Use the
            // actual block type for the request slot collection instead.
            sess.sourceName.mName=sess.expectedSource;
            sess.sourceName.mDynamicId=std::nullopt;
            int index=0;
            for(auto const& desc:*packet.mSlots) {
                if(index>=108)break;
                if(desc.mStackSize>0 && desc.mImpl && desc.mImpl->isValid()) {
                    auto name=desc.mImpl->getFullName();
                    if(!name.empty())sess.slots.push_back({
                        std::move(name),static_cast<int>(desc.mStackSize),
                        desc.mNetIdVariant,index});
                }
                ++index;
            }
            sess.phase=SilentSession::Phase::Ready;
            consumed=true;
            log("CONTENT_READY",index,static_cast<int>(sess.slots.size()));
            LHolo::getInstance().getSelf().getLogger().info(
                "MATERIAL_GRAB_CLIENT_ONLY_SOURCE_NAME id={} name={}",
                static_cast<int>(packet.mInventoryId),
                packet.mFullContainerName->toString());
            for(auto const& sourceSlot:sess.slots) {
                LHolo::getInstance().getSelf().getLogger().info(
                    "MATERIAL_GRAB_CLIENT_ONLY_SOURCE_SLOT id={} slot={} count={} net={}",
                    sourceSlot.id,sourceSlot.index,sourceSlot.amount,
                    sourceSlot.netId.toString());
            }
        }
    }
    if(!consumed)origin(source,packet);
}
LL_TYPE_INSTANCE_HOOK(SilentInventorySlotHook,ll::memory::HookPriority::Normal,
    LegacyClientNetworkHandler,&LegacyClientNetworkHandler::$handle,void,
    NetworkIdentifier const& source,InventorySlotPacket const& packet) {
    app::hook_lifecycle::DetourGuard guard;
    bool ignore=false;
    if(guard && installed.load(std::memory_order_acquire)) {
        std::lock_guard lock(mutex);
        ignore=sess.phase!=SilentSession::Phase::Idle
            && sess.containerId!=ContainerID::None
            && sess.containerId==packet.mInventoryId;
    }
    if(!ignore)origin(source,packet);
}
LL_TYPE_INSTANCE_HOOK(SilentResponseHook,ll::memory::HookPriority::Normal,
    ClientNetworkHandler,&ClientNetworkHandler::$handle,void,
    NetworkIdentifier const& source,ItemStackResponsePacket const& packet) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && installed.load(std::memory_order_acquire)) {
        std::lock_guard lock(mutex);
        if(sess.phase==SilentSession::Phase::Waiting && sess.requestId) {
            for(auto const& resp:*packet.mResponses) {
                if(resp.mClientRequestId->mRawId!=sess.requestId->mRawId)continue;
                log(resp.mResult==ItemStackNetResult::Success
                    ?"REQUEST_ACCEPTED":"REQUEST_REJECTED",
                    static_cast<int>(resp.mResult),sess.attempt);
                sess.phase=SilentSession::Phase::Closing;
                break;
            }
        }
    }
    // Normal net manager may not recognize silent requests, but must still
    // process unrelated item responses. Never fabricate client-side items.
    origin(source,packet);
}
LL_TYPE_INSTANCE_HOOK(SilentUseHook,ll::memory::HookPriority::High,
    GameMode,&GameMode::$useItemOn,InteractionResult,
    ItemStack& item,BlockPos const& at,uchar face,Vec3 const& hit,
    HandSlot handSlot,Block const* target,bool first) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && alt() && first && target) {
        static std::atomic<int> diagnosticCount{};
        if(diagnosticCount.fetch_add(1,std::memory_order_relaxed)<12)
            LHolo::getInstance().getSelf().getLogger().info(
                "MATERIAL_GRAB_CLIENT_ONLY_USE_TRACE enabled={} installed={} target={} hand={}",
                enabled(),installed.load(std::memory_order_acquire),
                target->getTypeName(),static_cast<int>(handSlot));
    }
    if(guard && installed.load(std::memory_order_acquire) && enabled()
        && alt() && first && handSlot==HandSlot::Mainhand
        && validContainerBlock(target)) {
        auto client=ll::service::getClientInstance();
        auto* player=client?client->getLocalPlayer():nullptr;
        auto hud=StructureUiState::getInstance().materialHudView();
        // A local world runs client and integrated-server Player instances
        // in the same process. Their pointers differ even for the same human.
        // Compare the server-assigned actor identity instead of addresses.
        bool const sameActor=player
            && mPlayer.getOrCreateUniqueID()==player->getOrCreateUniqueID();
        bool const hasStructure=static_cast<bool>(StructureSession::getInstance().loaded());
        bool const needs=hud && hud->ready && !hud->requirements.empty();
        if(!sameActor || !hasStructure || !needs) {
            static std::atomic<int> rejectedCount{};
            if(rejectedCount.fetch_add(1,std::memory_order_relaxed)<12)
                LHolo::getInstance().getSelf().getLogger().info(
                    "MATERIAL_GRAB_CLIENT_ONLY_ARM_REJECT"
                    " actorMatch={} pointerMatch={} structure={} needs={} player={}",
                    sameActor,player&&(&mPlayer==static_cast<Player*>(player)),
                    hasStructure,needs,player!=nullptr);
        }
        if(sameActor && hasStructure && needs) {
            std::lock_guard lock(mutex);
            if(sess.phase==SilentSession::Phase::Idle) {
                sess={};
                sess.phase=SilentSession::Phase::Armed;
                auto const blockId=target->getTypeName();
                sess.expectedSource=blockId=="minecraft:barrel"
                    ? ContainerEnumName::BarrelContainer
                    : ((blockId=="minecraft:shulker_box" ||
                        blockId.ends_with("_shulker_box"))
                        ? ContainerEnumName::ShulkerBoxContainer
                        : ContainerEnumName::LevelEntityContainer);
                sess.begun=GetTickCount64();
                log("ARMED");
            }
        }
    }
    return origin(item,at,face,hit,handSlot,target,first);
}
bool submitOne(LocalPlayer& player) {
    Slot found;
    int targetSlot=-1,amount=0,prior=0;
    FullContainerName sourceName;
    ContainerID openId{ContainerID::None};
    {
        std::lock_guard lock(mutex);
        if(sess.phase!=SilentSession::Phase::Ready)return false;
        bool complete=true;
        auto counts=structure::countInventoryItems(player.getInventory(),&complete);
        auto hud=StructureUiState::getInstance().materialHudView();
        if(!complete || !hud || !hud->ready){sess.phase=SilentSession::Phase::Closing;return false;}
        auto budget=computeGrabBudget(hud->requirements,counts,complete);
        auto& inventory=player.getInventory();
        for(auto const& src:sess.slots) {
            const int requested=grabAmountForSlot(budget,src.id,src.amount);
            if(requested<=0)continue;
            for(int index=0;index<inventory.getContainerSize() && index<36;++index) {
                auto const& dst=inventory.getItem(index);
                if(!dst.isNull())continue; // start only with empty slots (no merging risk).
                targetSlot=index;
                found=src;
                amount=std::min(64,requested);
                prior=counts.contains(src.id)?counts.at(src.id):0;
                break;
            }
            if(targetSlot>=0)break;
        }
        if(targetSlot<0) {sess.phase=SilentSession::Phase::Closing;return false;}
        sourceName=sess.sourceName;
        openId=sess.containerId;
        sess.phase=SilentSession::Phase::Waiting;
        sess.lastSent=GetTickCount64();
        sess.expectedItem=found.id;
        sess.beforeInventoryCount=prior;
        sess.attempt=amount;
    }
    auto client=ll::service::getClientInstance();
    if(!client){closeSession();return false;}
    // Use Mojang's packet factory to initialize the correct versioned packet.
    auto packet=MinecraftPackets::createPacket(MinecraftPacketIds::ItemStackRequest);
    if(!packet){closeSession();return false;}
    auto& req=static_cast<ItemStackRequestPacket&>(*packet);
    ItemStackRequestPacketData::RequestData entry{};
    ItemStackRequestId requestId{};
    requestId.clientInit();
    entry.mClientRequestId=requestId;
    // Native Shift-click uses a Place action when moving FROM the chest.
    // Match the actual request, not the InventoryContentPacket's placeholder.
    ItemStackRequestCereal::PlaceActionData place{};
    place.mAmount=static_cast<uchar>(amount);
    place.mSource->mFullContainerName=sourceName;
    place.mSource->mSlot=static_cast<uchar>(found.index);
    place.mSource->mNetIdVariant=found.netId;
    place.mDestination->mFullContainerName->mName=
        ContainerEnumName::CombinedHotbarAndInventoryContainer;
    place.mDestination->mFullContainerName->mDynamicId=std::nullopt;
    place.mDestination->mSlot=static_cast<uchar>(targetSlot);
    place.mDestination->mNetIdVariant=ItemStackNetIdVariant{};
    entry.mActions->emplace_back(place);
    req.mRequests->push_back(std::move(entry));
    {
        std::lock_guard lock(mutex);
        if(sess.phase!=SilentSession::Phase::Waiting)return false;
        sess.requestId=requestId;
    }
    LHolo::getInstance().getSelf().getLogger().info(
        "MATERIAL_GRAB_CLIENT_ONLY_DIRECT_REQUEST source={} slot={} net={} destination={} amount={} request={}",
        sourceName.toString(),found.index,found.netId.toString(),targetSlot,amount,requestId.mRawId);
    client->getPacketSender().sendToServer(*packet);
    log("SENT_DIRECT_ITEM_STACK_REQUEST",amount,targetSlot);
    return true;
}
LL_TYPE_INSTANCE_HOOK(SilentTickHook,ll::memory::HookPriority::Low,
    LocalPlayer,&LocalPlayer::$tickWorld,void,Tick const& tick) {
    app::hook_lifecycle::DetourGuard guard;
    origin(tick);
    if(!guard || !installed.load(std::memory_order_acquire))return;
    auto now=GetTickCount64();
    SilentSession::Phase phase;std::uint64_t start=0,last=0;
    {
        std::lock_guard lock(mutex);
        phase=sess.phase;start=sess.begun;last=sess.lastSent;
    }
    if(phase==SilentSession::Phase::Ready) {
        app::invokeNativeCallback([&]{submitOne(*this);},
            [](char const* err) noexcept {
                app::reportNativeCallbackFailure("client-only material transfer",err);
            });
    } else if(phase==SilentSession::Phase::Closing
        || (phase==SilentSession::Phase::Armed && now-start>kOpenTimeout)
        || (phase==SilentSession::Phase::Open && now-start>kContentTimeout)
        || (phase==SilentSession::Phase::Waiting && now-last>kRequestTimeout)) {
        closeSession();
    }
}
}
bool installClientOnlyGrabHooks() {
    installed=false;
    if(SilentContainerOpenHook::hook()!=0)return false;
    if(SilentInventoryContentHook::hook()!=0)goto failOpen;
    if(SilentInventorySlotHook::hook()!=0)goto failContent;
    if(SilentResponseHook::hook()!=0)goto failSlot;
    if(SilentUseHook::hook()!=0)goto failResponse;
    if(SilentTickHook::hook()!=0)goto failUse;
    requestTraceInstalled.store(SilentRequestTraceHook::hook()==0,
        std::memory_order_release);
    installed=true;
    LHolo::getInstance().getSelf().getLogger().info(
        "MATERIAL_GRAB_CLIENT_ONLY_HOOKS_READY enabled={}",enabled());
    return true;
failUse: SilentUseHook::unhook();
failResponse: SilentResponseHook::unhook();
failSlot: SilentInventorySlotHook::unhook();
failContent: SilentInventoryContentHook::unhook();
failOpen: SilentContainerOpenHook::unhook();
    return false;
}
bool uninstallClientOnlyGrabHooks() {
    installed=false;
    if(requestTraceInstalled.exchange(false,std::memory_order_acq_rel))
        SilentRequestTraceHook::unhook();
    SilentTickHook::unhook();SilentUseHook::unhook();
    SilentResponseHook::unhook();SilentInventorySlotHook::unhook();
    SilentInventoryContentHook::unhook();SilentContainerOpenHook::unhook();
    return true;
}
} // namespace lholo::structure::detail
