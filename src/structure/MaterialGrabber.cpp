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
#include "mc/client/gui/screens/ScreenView.h"
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
// Cross-thread display gate: no engine pointers are dereferenced through this
// atomic; only exact pointer identity is compared in ScreenView::render.
std::atomic<ScreenController const*> suppressedScreen{};
struct TransferReceipt {
    std::string id, collection;
    int sourceSlot{}, sourceCount{}, inventoryCount{}, attempted{};
    std::uint64_t since{};
};
struct GrabSession {
    ContainerScreenController const* screen{};
    bool armed{}, finished{}, closeIssued{};
    std::uint64_t openedAt{}, nextAction{}, closeAt{};
    std::optional<TransferReceipt> pending;
};
thread_local GrabSession session;
// The player must explicitly opt in. Regular right-click always works normally.
// Holding Alt while opening the chest/shulker GUI arms ONE collection session.
bool altPressed() noexcept { return (GetAsyncKeyState(VK_MENU)&0x8000)!=0; }
void stopSuppression(ContainerScreenController const* expected) {
    auto* pointer=static_cast<ScreenController const*>(expected);
    suppressedScreen.compare_exchange_strong(pointer,nullptr,std::memory_order_acq_rel);
}
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
    if (!player || !controller) {
        // Bounded initialization; if the GUI never becomes usable, restore its
        // visibility so the player can exit manually.
        if(now-session.openedAt>1600) {
            session.armed=false;
            stopSuppression(session.screen);
        }
        session.nextAction=now+120;
        return;
    }
    if(!supportedContainer(*controller)) {
        // Alt used on a furnace/crafting GUI: always hand control back.
        session.armed=false;
        stopSuppression(session.screen);
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
    // Native screen must exist for valid server-authoritative item transfers.
    // It remains completely unrendered during the short collection session.
    auto const hud=StructureUiState::getInstance().materialHudView();
    auto const loaded=StructureSession::getInstance().loaded();
    bool const armed=altPressed() && loaded && hud && hud->ready
        && !hud->requirements.empty();
    session=GrabSession{this,armed,false,false,GetTickCount64(),
        GetTickCount64()+150,0,{}};
    if(armed)
        suppressedScreen.store(static_cast<ScreenController const*>(this),
            std::memory_order_release);
}
LL_TYPE_INSTANCE_HOOK(MaterialGrabTickHook,ll::memory::HookPriority::Low,
    ContainerScreenController,&ContainerScreenController::$tick,::ui::DirtyFlag) {
    app::hook_lifecycle::DetourGuard guard;
    auto native=origin();
    if(!guard || !available.load(std::memory_order_acquire))return native;
    if(session.screen!=this || !session.armed)return native;
    if(!session.finished) {
        app::invokeNativeCallback([&] {collectOne(*this);},[](char const* reason) noexcept {
            app::reportNativeCallbackFailure("material grab from container",reason);
        });
    }
    if(session.finished && !session.closeIssued) {
        session.closeIssued=true;
        session.closeAt=GetTickCount64();
        // Ask Bedrock to close both the native UI and the server container.
        // Never destroy a screen or manipulate the stack directly in its tick.
        auto client=ll::service::getClientInstance();
        auto* player=client?client->getLocalPlayer():nullptr;
        if(player)player->closeContainerScreen(SharedTypes::Legacy::ContainerType::Container);
        else {session.armed=false;stopSuppression(this);}
        return native;
    }
    if(session.closeIssued && GetTickCount64()-session.closeAt>2400) {
        // Never strand the user inside an invisible native UI on failed close.
        session.armed=false;
        stopSuppression(this);
    }
    return native;
}
// The server still gets a normal chest session, but the window is never drawn.
// Native controller ticks and transfer notifications are deliberately intact.
LL_TYPE_INSTANCE_HOOK(MaterialGrabRenderHook,ll::memory::HookPriority::Low,
    ScreenView,&ScreenView::render,void,UIRenderContext& renderContext) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && available.load(std::memory_order_acquire)
        && mController.get()==suppressedScreen.load(std::memory_order_acquire))
        return;
    origin(renderContext);
}
LL_TYPE_INSTANCE_HOOK(MaterialGrabLeaveHook,ll::memory::HookPriority::Low,
    ContainerScreenController,&ContainerScreenController::$onLeave,void) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && session.screen==this) {
        stopSuppression(this);
        session={};
    }
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
    if(MaterialGrabRenderHook::hook()!=0) {
        MaterialGrabLeaveHook::unhook();MaterialGrabTickHook::unhook();
        MaterialGrabOpenHook::unhook();return false;
    }
    available.store(true,std::memory_order_release);
    return true;
}
bool uninstallMaterialGrabberHook() {
    available.store(false,std::memory_order_release);
    suppressedScreen.store(nullptr,std::memory_order_release);
    session={};
    auto d=MaterialGrabRenderHook::unhook();
    auto a=MaterialGrabLeaveHook::unhook();
    auto b=MaterialGrabTickHook::unhook();
    auto c=MaterialGrabOpenHook::unhook();
    return d&&a&&b&&c;
}
}
