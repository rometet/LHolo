// One-shot, opt-in material grabbing for the native Bedrock container GUI.
#include "structure/MaterialGrabber.h"
#include "structure/MaterialGrabRules.h"
#include "structure/InventoryContents.h"
#include "structure/StructureUiState.h"
#include "structure/StructureSession.h"
#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/containers/models/ContainerModel.h"
#include "mc/world/containers/controllers/ContainerController.h"
#include "mc/world/containers/models/ContainerCategory.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/item/ItemStack.h"
#include <Windows.h>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lholo::structure::detail {
namespace {
std::atomic_bool available{};
struct TransferReceipt {
    std::string id, collection;
    int sourceSlot{}, sourceCount{}, inventoryCount{}, attempted{};
    std::uint64_t since{};
};
struct GrabSession {
    ContainerScreenController const* screen{};
    bool armed{}, finished{};
    std::uint64_t openedAt{}, nextAction{};
    std::optional<TransferReceipt> pending;
};
thread_local GrabSession session;
// The player must explicitly opt in. Regular right-click always works normally.
// Holding Ctrl while opening the chest/shulker GUI arms ONE collection session.
bool ctrlPressed() noexcept { return (GetAsyncKeyState(VK_CONTROL)&0x8000)!=0; }
bool supportedContainer(ContainerManagerController const& controller) {
    auto manager=controller.mContainerManagerModel.lock();
    return manager && manager->getContainerType()==SharedTypes::Legacy::ContainerType::Container;
}
void collectOne(ContainerScreenController& screen) {
    if (!session.armed || session.finished) return;
    auto const now=GetTickCount64();
    if (now<session.nextAction) return;
    auto client=ll::service::getClientInstance();
    auto* player=client ? client->getLocalPlayer() : nullptr;
    auto controller=screen.mContainerManagerController.get();
    if (!player || !controller || !supportedContainer(*controller)) {
        // Delay for the first few GUI ticks while the native screen initializes.
        if(now-session.openedAt>4000) session.finished=true;
        session.nextAction=now+120;
        return;
    }
    auto& ui=StructureUiState::getInstance();
    auto hud=ui.materialHudView();
    auto loaded=StructureSession::getInstance().loaded();
    if (!loaded || !hud || !hud->ready || hud->requirements.empty()) {
        session.finished=true; return;
    }
    bool complete=true;
    auto inventory=structure::countInventoryItems(player->getInventory(), &complete);
    if (!complete) { session.finished=true; return; }
    if(session.pending) {
        auto const& receipt=*session.pending;
        auto have=inventory.find(receipt.id);
        int const current=have==inventory.end()?0:have->second;
        auto const& sources=controller->mContainers.get();
        auto src=sources.find(receipt.collection);
        if (src==sources.end() || !src->second) { session.finished=true;return; }
        auto model=src->second->mContainerModel.lock();
        if (!model || receipt.sourceSlot>=model->getContainerSize()) {
            session.finished=true;return;
        }
        auto const& remaining=model->getItemStack(receipt.sourceSlot);
        bool const sourceReduced=remaining.isNull()
            || remaining.getTypeName()!=receipt.id
            || static_cast<int>(remaining.mCount)<=receipt.sourceCount-receipt.attempted;
        bool const inventoryIncreased=static_cast<std::int64_t>(current)>=static_cast<std::int64_t>(receipt.inventoryCount)+receipt.attempted;
        if(sourceReduced && inventoryIncreased) {
            session.pending.reset();
            session.nextAction=now+100;
        } else if(now-receipt.since>1200) {
            // No verified receipt: do not repeat an ambiguous move.
            session.finished=true;
        }
        return;
    }
    auto budget=computeGrabBudget(hud->requirements,inventory,complete);
    if (budget.empty()) {session.finished=true;return;}
    // Use only the vanilla controller's already-registered auto-place order.
    // Never synthesize an inventory transaction or edit a server container.
    auto const& sources=controller->mContainers.get();
    auto const& orders=screen.mAutoPlaceOrderMap.get();
    for(auto const& [collection, container] : sources) {
        if(!container)continue;
        auto model=container->mContainerModel.lock();
        if(!model || model->mContainerCategory!=ContainerCategory::Default
            || model->getContainerSize()<=0 || model->getContainerSize()>108)continue;
        if (!orders.contains(collection) || orders.at(collection).empty())continue;
        for(int slot=0;slot<model->getContainerSize();++slot) {
            auto const& stack=model->getItemStack(slot);
            if(stack.isNull() || stack.mCount<=0)continue;
            auto id=stack.getTypeName();
            int const amount=grabAmountForSlot(budget,id,static_cast<int>(stack.mCount));
            if(!amount)continue;
            int const previous=inventory.contains(id)?inventory.at(id):0;
            int const before=static_cast<int>(stack.mCount);
            screen._handleAutoPlace(amount,collection,slot);
            session.pending=TransferReceipt{
                std::move(id),collection,slot,before,previous,
                amount,now
            };
            session.nextAction=now+100;
            return;
        }
    }
    // No matching accessible stack in this GUI, or no valid vanilla destination.
    session.finished=true;
}
LL_TYPE_INSTANCE_HOOK(MaterialGrabOpenHook,ll::memory::HookPriority::Low,
    ContainerScreenController,&ContainerScreenController::$onOpen,void) {
    app::hook_lifecycle::DetourGuard guard;
    origin();
    if(!guard || !available.load(std::memory_order_acquire))return;
    // Opt-in while opening; never grab from an ordinary right-click.
    session=GrabSession{this,ctrlPressed(),false,GetTickCount64(),GetTickCount64()+150,{}};
}
LL_TYPE_INSTANCE_HOOK(MaterialGrabTickHook,ll::memory::HookPriority::Low,
    ContainerScreenController,&ContainerScreenController::$tick,::ui::DirtyFlag) {
    app::hook_lifecycle::DetourGuard guard;
    auto native=origin();
    if(!guard || !available.load(std::memory_order_acquire))return native;
    if(session.screen!=this || !session.armed || session.finished)return native;
    app::invokeNativeCallback([&] {collectOne(*this);},[](char const* reason) noexcept {
        app::reportNativeCallbackFailure("material grab from container",reason);
    });
    return native;
}
LL_TYPE_INSTANCE_HOOK(MaterialGrabLeaveHook,ll::memory::HookPriority::Low,
    ContainerScreenController,&ContainerScreenController::$onLeave,void) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && session.screen==this)session={};
    origin();
}
} // namespace
bool installMaterialGrabberHook() {
    available.store(false,std::memory_order_release);
    if(MaterialGrabOpenHook::hook()!=0)return false;
    if(MaterialGrabTickHook::hook()!=0) {
        MaterialGrabOpenHook::unhook();return false;
    }
    if(MaterialGrabLeaveHook::hook()!=0) {
        MaterialGrabTickHook::unhook();MaterialGrabOpenHook::unhook();return false;
    }
    available.store(true,std::memory_order_release);
    return true;
}
bool uninstallMaterialGrabberHook() {
    available.store(false,std::memory_order_release);
    session={};
    auto a=MaterialGrabLeaveHook::unhook();
    auto b=MaterialGrabTickHook::unhook();
    auto c=MaterialGrabOpenHook::unhook();
    return a&&b&&c;
}
}
