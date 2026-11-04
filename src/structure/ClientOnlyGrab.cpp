// Client-only, experimental server-authoritative material grabbing.
// Unlike the old hidden native chest, the client does not install a UIScene.
// It accepts the server's real ContainerOpen/InventoryContent then dispatches
// one normal ItemStackRequest. No server behavior pack or custom command.
#include "structure/ClientOnlyGrab.h"
#include "structure/MaterialTracker.h"
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
#include "mc/world/item/NetworkItemStackDescriptor.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/dimension/Dimension.h"
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
#include "mc/network/packet/InventoryTransactionPacket.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/entity/components/PackedItemUseLegacyInventoryTransaction.h"
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
#include <memory>
#include <algorithm>
#include <variant>

namespace lholo::structure::detail {
namespace {
constexpr std::uint64_t kOpenTimeout=5000, kContentTimeout=3000, kRequestTimeout=3500;
std::atomic_bool installed{false};
std::atomic_bool preferenceEnabled{false};
std::atomic_int grabModifiers{2}; // default Alt+right-click
constexpr int kSupportedModifiers=7;
constexpr int kMaxRequestsPerActivation=108;
constexpr std::size_t kBatchActions=36;
constexpr std::uint64_t kMaxSessionMs=30000;
struct Slot {
    std::string id;
    int amount{};
    ItemStackNetIdVariant netId;
    int index{};
    std::shared_ptr<NetworkItemStackDescriptor> descriptor;
};
struct BatchReceipt {
    int sourceSlot{-1},destinationSlot{-1},amount{};
    std::string itemId;
    std::shared_ptr<NetworkItemStackDescriptor> descriptor;
    int approvedAmount{};
    std::optional<ItemStackNetId> approvedNetId;
};
struct SilentSession {
    enum class Phase {Idle, Armed, Open, Ready, Waiting, AwaitingInventory, Closing};
    Phase phase{Phase::Idle};
    std::uint64_t begun{},lastSent{};
    ContainerID containerId{ContainerID::None};
    SharedTypes::Legacy::ContainerType type{SharedTypes::Legacy::ContainerType::Container};
    FullContainerName sourceName{};
    ContainerEnumName expectedSource{ContainerEnumName::LevelEntityContainer};
    std::vector<Slot> slots;
    std::optional<ItemStackRequestId> requestId;
    std::vector<BatchReceipt> batch;
    int successfulRequests{};
    std::uint64_t readyAfter{};
};
std::mutex mutex;
SilentSession sess;

// MCAPI 26.60 does not export toString() for either diagnostic type.
// Preserve the same numeric identity in logs without linking engine code.
std::string containerDebugName(FullContainerName const& name) {
    std::string result=std::to_string(static_cast<unsigned>(name.mName));
    if(name.mDynamicId.get())result+=":"+std::to_string(*name.mDynamicId.get());
    return result;
}
std::string netVariantDebugName(ItemStackNetIdVariant const& net) {
    return std::visit([](auto const& value) {
        return std::to_string(value.mRawId);
    },net.mVariant.get());
}
bool enabled() noexcept {
    return preferenceEnabled.load(std::memory_order_acquire);
}
int heldModifiers() noexcept {
    return ((GetAsyncKeyState(VK_CONTROL)&0x8000) ? 1 : 0)
        | ((GetAsyncKeyState(VK_MENU)&0x8000) ? 2 : 0)
        | ((GetAsyncKeyState(VK_SHIFT)&0x8000) ? 4 : 0);
}
bool chordPressed() noexcept {
    const int configured=grabModifiers.load(std::memory_order_acquire);
    return configured>0 && heldModifiers()==configured;
}
bool validContainerBlock(Block const* block) {
    if(!block)return false;
    auto const id=block->getTypeName();
    return id=="minecraft:chest" || id=="minecraft:trapped_chest"
        || id=="minecraft:barrel" || id=="minecraft:shulker_box"
        || (id.starts_with("minecraft:") && id.ends_with("_shulker_box"));
}
// Arm on the actual outgoing right-click packet as well as GameMode.
// Some remote-server interactions skip GameMode::useItemOn(first=true).
// The packet supplies the authoritative clicked position. Never arm for
// arbitrary GUI openings or non-container targets.
bool armFromPacketBlock(BlockPos const& pos,char const* trigger) {
    if(!installed.load(std::memory_order_acquire) || !enabled() || !chordPressed())return false;
    auto client=ll::service::getClientInstance();
    auto* player=client?client->getLocalPlayer():nullptr;
    if(!player || !StructureSession::getInstance().loaded())return false;
    auto hud=StructureUiState::getInstance().materialHudView();
    if(!hud || !hud->ready || hud->requirements.empty())return false;
    auto const& block=player->getDimension()
        .getBlockSourceFromMainChunkSource().getBlock(pos);
    if(!validContainerBlock(&block))return false;
    bool complete=true;
    auto const carried=structure::countInventoryItems(player->getInventory(),&complete);
    if(computeGrabBudget(hud->requirements,carried,complete).empty())return false;
    std::lock_guard lock(mutex);
    if(sess.phase!=SilentSession::Phase::Idle)return false;
    sess={};
    sess.phase=SilentSession::Phase::Armed;
    sess.begun=GetTickCount64();
    auto const& id=block.getTypeName();
    sess.expectedSource=id=="minecraft:barrel"
        ? ContainerEnumName::BarrelContainer
        : (id=="minecraft:shulker_box" || id.ends_with("_shulker_box"))
            ? ContainerEnumName::ShulkerBoxContainer
            : ContainerEnumName::LevelEntityContainer;
    LHolo::getInstance().getSelf().getLogger().info(
        "MATERIAL_GRAB_CLIENT_ONLY_ARMED_FROM_PACKET source={} block={}",trigger,id);
    return true;
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
    if(guard && installed.load(std::memory_order_acquire) && enabled() && chordPressed()) {
        if(packet.getId()==MinecraftPacketIds::InventoryTransaction) {
            auto& tx=static_cast<InventoryTransactionPacket&>(packet);
            if(auto* use=std::get_if<ItemUseInventoryTransaction>(
                    &*tx.mVariantTransaction)) {
                if(use->mActionType==ItemUseInventoryTransaction::ActionType::Use)
                    armFromPacketBlock(*use->mPos,"InventoryTransaction");
            }
        } else if(packet.getId()==MinecraftPacketIds::PlayerAuthInputPacket) {
            auto& input=static_cast<PlayerAuthInputPacket&>(packet);
            if(input.mItemUseTransaction) {
                auto const& use=input.mItemUseTransaction->mTransaction;
                if(use->mActionType==ItemUseInventoryTransaction::ActionType::Use)
                    armFromPacketBlock(*use->mPos,"PlayerAuthInput");
            }
        }
    }
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
                            containerDebugName(*take->mSource->mFullContainerName),
                            static_cast<int>(take->mSource->mSlot),
                            netVariantDebugName(*take->mSource->mNetIdVariant),
                            containerDebugName(*take->mDestination->mFullContainerName),
                            static_cast<int>(take->mDestination->mSlot),
                            netVariantDebugName(*take->mDestination->mNetIdVariant),
                            static_cast<int>(take->mAmount));
                    } else if(auto const* place=std::get_if<ItemStackRequestCereal::PlaceActionData>(&action)) {
                        LHolo::getInstance().getSelf().getLogger().info(
                            "MATERIAL_GRAB_CLIENT_ONLY_PACKET_PLACE src={} slot={} net={} dst={} slot={} net={} amount={}",
                            containerDebugName(*place->mSource->mFullContainerName),
                            static_cast<int>(place->mSource->mSlot),
                            netVariantDebugName(*place->mSource->mNetIdVariant),
                            containerDebugName(*place->mDestination->mFullContainerName),
                            static_cast<int>(place->mDestination->mSlot),
                            netVariantDebugName(*place->mDestination->mNetIdVariant),
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
        // Remote servers can skip the local GameMode hook altogether.
        // Last-chance arming happens BEFORE origin() can create a UIScene.
        if(packet.mType==SharedTypes::Legacy::ContainerType::Container)
            armFromPacketBlock(*packet.mPos,"ContainerOpen");
        std::lock_guard lock(mutex);
        if(enabled() && chordPressed()) {
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
                        desc.mNetIdVariant,index,
                        std::make_shared<NetworkItemStackDescriptor>(desc)});
                }
                ++index;
            }
            sess.phase=SilentSession::Phase::Ready;
            consumed=true;
            log("CONTENT_READY",index,static_cast<int>(sess.slots.size()));
            LHolo::getInstance().getSelf().getLogger().info(
                "MATERIAL_GRAB_CLIENT_ONLY_SOURCE_NAME id={} name={}",
                static_cast<int>(packet.mInventoryId),
                containerDebugName(packet.mFullContainerName.get()));
            for(auto const& sourceSlot:sess.slots) {
                LHolo::getInstance().getSelf().getLogger().info(
                    "MATERIAL_GRAB_CLIENT_ONLY_SOURCE_SLOT id={} slot={} count={} net={}",
                    sourceSlot.id,sourceSlot.index,sourceSlot.amount,
                    netVariantDebugName(sourceSlot.netId));
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
                bool const accepted=resp.mResult==ItemStackNetResult::Success;
                log(accepted?"BATCH_ACCEPTED":"BATCH_REJECTED",
                    static_cast<int>(resp.mResult),static_cast<int>(sess.batch.size()));
                if(accepted) {
                    for(auto const& updated:*resp.mContainers) {
                        const auto name=updated.mFullContainerName->mName;
                        if(name!=ContainerEnumName::CombinedHotbarAndInventoryContainer
                            && name!=ContainerEnumName::HotbarContainer
                            && name!=ContainerEnumName::InventoryContainer)continue;
                        for(auto const& slot:*updated.mSlots) {
                            int const requested=static_cast<int>(slot.mRequestedSlot);
                            int const actual=static_cast<int>(slot.mSlot);
                            int const quantity=static_cast<int>(slot.mAmount);
                            auto const netId=*slot.mItemStackNetId;
                            for(auto& move:sess.batch) {
                                if(requested!=move.destinationSlot
                                    && actual!=move.destinationSlot)continue;
                                if(quantity<1 || quantity>move.amount || netId.mRawId<=0)
                                    continue;
                                move.approvedAmount=quantity;
                                move.approvedNetId=netId;
                                LHolo::getInstance().getSelf().getLogger().info(
                                    "MATERIAL_GRAB_CLIENT_ONLY_BATCH_ACK_SLOT"
                                    " dst={} amount={} net={}",
                                    move.destinationSlot,quantity,netId.mRawId);
                            }
                        }
                    }
                    // Never reuse an old source NetId, including partial takes.
                    for(auto const& move:sess.batch)
                        std::erase_if(sess.slots,[&](Slot const& slot) {
                            return slot.index==move.sourceSlot;
                        });
                    sess.successfulRequests+=static_cast<int>(sess.batch.size());
                }
                sess.phase=accepted
                    ? SilentSession::Phase::AwaitingInventory
                    : SilentSession::Phase::Closing;
                break;
            }
        }
    }
    origin(source,packet);
}
LL_TYPE_INSTANCE_HOOK(SilentUseHook,ll::memory::HookPriority::High,
    GameMode,&GameMode::$useItemOn,InteractionResult,
    ItemStack& item,BlockPos const& at,uchar face,Vec3 const& hit,
    HandSlot handSlot,Block const* target,bool first) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && chordPressed() && first && target) {
        static std::atomic<int> diagnosticCount{};
        if(diagnosticCount.fetch_add(1,std::memory_order_relaxed)<12)
            LHolo::getInstance().getSelf().getLogger().info(
                "MATERIAL_GRAB_CLIENT_ONLY_USE_TRACE enabled={} installed={} target={} hand={}",
                enabled(),installed.load(std::memory_order_acquire),
                target->getTypeName(),static_cast<int>(handSlot));
    }
    if(guard && installed.load(std::memory_order_acquire) && enabled()
        && chordPressed() && first && handSlot==HandSlot::Mainhand
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
bool submitBatch(LocalPlayer& player) {
    std::vector<Slot> selectedSources;
    std::vector<GrabBatchMove> plan;
    FullContainerName sourceName;
    {
        std::lock_guard lock(mutex);
        if(sess.phase!=SilentSession::Phase::Ready)return false;
        auto const now=GetTickCount64();
        if(now<sess.readyAfter)return false;
        if(now-sess.begun>kMaxSessionMs
            || sess.successfulRequests>=kMaxRequestsPerActivation) {
            sess.phase=SilentSession::Phase::Closing;
            return false;
        }
        bool complete=true;
        auto counts=structure::countInventoryItems(player.getInventory(),&complete);
        auto hud=StructureUiState::getInstance().materialHudView();
        if(!complete || !hud || !hud->ready){
            sess.phase=SilentSession::Phase::Closing;
            return false;
        }
        auto budget=computeGrabBudget(hud->requirements,counts,complete);
        auto& inventory=player.getInventory();
        std::vector<int> empty;
        for(int i=0;i<inventory.getContainerSize() && i<36;++i)
            if(inventory.getItem(i).isNull())empty.push_back(i);
        std::vector<GrabBatchSource> sources;
        sources.reserve(sess.slots.size());
        for(auto const& source:sess.slots)
            sources.push_back({source.index,source.id,source.amount});
        plan=planGrabBatch(std::move(budget),sources,empty,
            std::min(kBatchActions,
                static_cast<std::size_t>(kMaxRequestsPerActivation-sess.successfulRequests)));
        if(plan.empty()) {
            sess.phase=SilentSession::Phase::Closing;
            return false;
        }
        for(auto const& action:plan) {
            auto it=std::find_if(sess.slots.begin(),sess.slots.end(),
                [&](Slot const& source){return source.index==action.sourceSlot;});
            if(it==sess.slots.end() || !it->descriptor) {
                sess.phase=SilentSession::Phase::Closing;
                return false;
            }
            selectedSources.push_back(*it);
        }
        sourceName=sess.sourceName;
        sess.batch.clear();
        for(std::size_t i=0;i<plan.size();++i)
            sess.batch.push_back({
                plan[i].sourceSlot,plan[i].destinationSlot,plan[i].amount,
                plan[i].itemId,selectedSources[i].descriptor
            });
        sess.phase=SilentSession::Phase::Waiting;
        sess.lastSent=GetTickCount64();
    }
    auto client=ll::service::getClientInstance();
    if(!client){closeSession();return false;}
    auto packet=MinecraftPackets::createPacket(MinecraftPacketIds::ItemStackRequest);
    if(!packet){closeSession();return false;}
    auto& req=static_cast<ItemStackRequestPacket&>(*packet);
    ItemStackRequestPacketData::RequestData entry{};
    ItemStackRequestId requestId{};
    requestId.clientInit();
    entry.mClientRequestId=requestId;
    for(std::size_t i=0;i<plan.size();++i) {
        ItemStackRequestCereal::PlaceActionData place{};
        place.mAmount=static_cast<uchar>(plan[i].amount);
        place.mSource->mFullContainerName=sourceName;
        place.mSource->mSlot=static_cast<uchar>(plan[i].sourceSlot);
        place.mSource->mNetIdVariant=selectedSources[i].netId;
        place.mDestination->mFullContainerName->mName=
            ContainerEnumName::CombinedHotbarAndInventoryContainer;
        place.mDestination->mFullContainerName->mDynamicId=std::nullopt;
        place.mDestination->mSlot=static_cast<uchar>(plan[i].destinationSlot);
        place.mDestination->mNetIdVariant=ItemStackNetIdVariant{};
        entry.mActions->emplace_back(place);
    }
    req.mRequests->push_back(std::move(entry));
    {
        std::lock_guard lock(mutex);
        if(sess.phase!=SilentSession::Phase::Waiting)return false;
        sess.requestId=requestId;
    }
    LHolo::getInstance().getSelf().getLogger().info(
        "MATERIAL_GRAB_CLIENT_ONLY_BATCH_REQUEST actions={} request={}",
        plan.size(),requestId.mRawId);
    client->getPacketSender().sendToServer(*packet);
    log("SENT_BATCH_REQUEST",static_cast<int>(plan.size()));
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
    if(phase!=SilentSession::Phase::Idle && now-start>kMaxSessionMs) {
        closeSession();
        return;
    }
    if(phase==SilentSession::Phase::Ready) {
        app::invokeNativeCallback([&]{submitBatch(*this);},
            [](char const* err) noexcept {
                app::reportNativeCallbackFailure("client-only material transfer",err);
            });
    } else if(phase==SilentSession::Phase::AwaitingInventory) {
        app::invokeNativeCallback([&] {
            std::vector<BatchReceipt> moves;
            {
                std::lock_guard lock(mutex);
                if(sess.phase!=SilentSession::Phase::AwaitingInventory)return;
                moves=sess.batch;
            }
            auto& inventory=this->getInventory();
            bool complete=true;
            bool conflicted=false;
            int present=0;
            for(auto const& move:moves) {
                if(move.destinationSlot<0 || move.destinationSlot>=inventory.getContainerSize()) {
                    conflicted=true;break;
                }
                auto const& existing=inventory.getItem(move.destinationSlot);
                if(!existing.isNull()) {
                    int const count=static_cast<int>(existing.mCount);
                    if(existing.getTypeName()!=move.itemId
                        || count<1 || count>move.amount
                        || (move.approvedAmount>0 && count!=move.approvedAmount)) {
                        conflicted=true;break;
                    }
                    ++present;
                    continue;
                }
                // Apply only server-returned slot quantities and positive NetId,
                // never speculate based on a request or a success flag alone.
                if(now-last>=20 && move.descriptor && move.approvedNetId
                    && move.approvedAmount>0
                    && move.approvedAmount<=move.amount) {
                    ItemStack received=ItemStack::fromDescriptor(
                        *move.descriptor,this->getLevel().getBlockPalette(),true);
                    if(received.isNull() || received.getTypeName()!=move.itemId) {
                        conflicted=true;break;
                    }
                    received.set(move.approvedAmount);
                    received.mNetIdVariant->mVariant=*move.approvedNetId;
                    inventory.setItem(move.destinationSlot,received);
                    ++present;
                } else complete=false;
            }
            if(conflicted) {
                log("BATCH_DESTINATION_CONFLICT",present,static_cast<int>(moves.size()));
                closeSession();
                return;
            }
            if(complete && present==static_cast<int>(moves.size())) {
                {
                    std::lock_guard lock(mutex);
                    if(sess.phase!=SilentSession::Phase::AwaitingInventory)return;
                    sess.phase=SilentSession::Phase::Ready;
                    sess.readyAfter=now+50;
                    sess.requestId.reset();
                    sess.batch.clear();
                }
                log("BATCH_INVENTORY_CONFIRMED",present);
                requestMaterialHudAvailabilityRefresh();
            } else if(now-last>2500) {
                // Stop on missing authoritative data; no duplicate request.
                log("BATCH_RECONCILIATION_TIMEOUT",present,
                    static_cast<int>(moves.size()));
                closeSession();
            }
        }, [](char const* err) noexcept {
            app::reportNativeCallbackFailure("client-only batch reconciliation",err);
        });
    } else if(phase==SilentSession::Phase::Closing
        || (phase==SilentSession::Phase::Armed && now-start>kOpenTimeout)
        || (phase==SilentSession::Phase::Open && now-start>kContentTimeout)
        || (phase==SilentSession::Phase::Waiting && now-last>kRequestTimeout)) {
        closeSession();
    }
}
}
bool clientOnlyGrabEnabled() noexcept { return enabled(); }
int clientOnlyGrabModifiers() noexcept {
    return grabModifiers.load(std::memory_order_acquire);
}
bool clientOnlyGrabChordPressed() noexcept { return chordPressed(); }
void setClientOnlyGrabModifiers(int modifiers) {
    if(modifiers<1 || modifiers>kSupportedModifiers)modifiers=2;
    const int previous=grabModifiers.exchange(modifiers,std::memory_order_acq_rel);
    if(previous!=modifiers)closeSession();
}

bool hasLegacyClientOnlyGrabMarker() noexcept {
    // Migration only. Never consulted during normal runtime operations.
    constexpr auto file=L".\\mods\\LHolo\\config\\client_only_grab.enabled";
    auto const flags=GetFileAttributesW(file);
    return flags!=INVALID_FILE_ATTRIBUTES && !(flags&FILE_ATTRIBUTE_DIRECTORY);
}

void setClientOnlyGrabEnabled(bool enable) {
    bool const previous=preferenceEnabled.exchange(enable,std::memory_order_acq_rel);
    // Stop an in-flight silent session immediately on switch-off so no stale
    // response can be accepted after the user disables the experimental path.
    if(previous && !enable) closeSession();
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
