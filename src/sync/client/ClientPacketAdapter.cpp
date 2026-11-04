#include "sync/client/ClientPacketAdapter.h"

#include <memory>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/network/packet/Packet.h"

#include "sync/client/SyncClient.h"
#include "sync/wire/SyncFramePacket.h"

namespace lholo::sync {

namespace {

// Registered with the LL PacketRegistrar at static-init time by instantiating
// PacketHandlerBase<Derived, PacketType> (the documented LL pattern). The body
// only copies already-validated bytes into the SyncClient queue (§3.2).
class ClientSyncFrameHandler final
: public ll::network::PacketHandlerBase<ClientSyncFrameHandler, SyncFramePacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback&, SyncFramePacket const& packet) const {
        SyncClient::instance().onFrame(packet.frame);
    }
};

// Force template instantiation (and thus registration) in this TU.
[[maybe_unused]] ClientSyncFrameHandler s_clientFrameHandler;

std::shared_ptr<ll::event::Listener<ll::event::client::ClientJoinLevelEvent>> g_joinListener;
std::shared_ptr<ll::event::Listener<ll::event::client::ClientExitLevelEvent>> g_exitListener;
std::shared_ptr<ll::event::Listener<ll::event::world::ClientLevelTickEvent>> g_tickListener;

} // namespace

[[nodiscard]] bool sendFrameToServer(std::vector<std::uint8_t> const& frame) {
    if (frame.empty()) {
        return false;
    }
    SyncFramePacket packet(frame);
    packet.sendToServer();
    return true;
}

void syncLogInfo(std::string const& message) {
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info(message);
    }
}

void syncLogWarn(std::string const& message) {
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().warn(message);
    }
}

void syncLogError(std::string const& message) {
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().error(message);
    }
}

bool installClientPacketAdapter() {
    auto& bus = ll::event::EventBus::getInstance();
    if (g_joinListener || g_exitListener || g_tickListener) {
        return true; // already installed
    }

    g_joinListener = bus.emplaceListener<ll::event::client::ClientJoinLevelEvent>(
        [](ll::event::client::ClientJoinLevelEvent&) { SyncClient::instance().onWorldJoin(); }
    );
    g_exitListener = bus.emplaceListener<ll::event::client::ClientExitLevelEvent>(
        [](ll::event::client::ClientExitLevelEvent&) { SyncClient::instance().onWorldLeave(); }
    );
    g_tickListener = bus.emplaceListener<ll::event::world::ClientLevelTickEvent>(
        [](ll::event::world::ClientLevelTickEvent&) { SyncClient::instance().tick(); }
    );

    if (!g_joinListener || !g_exitListener || !g_tickListener) {
        uninstallClientPacketAdapter();
        return false;
    }
    return true;
}

void uninstallClientPacketAdapter() noexcept {
    auto& bus = ll::event::EventBus::getInstance();
    if (g_joinListener) {
        bus.removeListener<ll::event::client::ClientJoinLevelEvent>(g_joinListener);
        g_joinListener.reset();
    }
    if (g_exitListener) {
        bus.removeListener<ll::event::client::ClientExitLevelEvent>(g_exitListener);
        g_exitListener.reset();
    }
    if (g_tickListener) {
        bus.removeListener<ll::event::world::ClientLevelTickEvent>(g_tickListener);
        g_tickListener.reset();
    }
}

} // namespace lholo::sync
