#include "ServerPacketAdapter.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/network/packet/Packet.h"

#include "SyncService.h"

#include "sync/wire/SyncFramePacket.h"

namespace lholo::sync {

namespace {

class ServerSyncFrameHandler final
: public ll::network::PacketHandlerBase<ServerSyncFrameHandler, SyncFramePacket> {
public:
    void handlePacket(NetworkIdentifier const& source, NetEventCallback&, SyncFramePacket const& packet) const {
        // Receive-thread: validate (already bounded by the packet read) and copy.
        SyncService::instance().enqueueInbound(source, packet.frame);
    }
};

[[maybe_unused]] ServerSyncFrameHandler s_serverFrameHandler;

std::shared_ptr<ll::event::Listener<ll::event::world::ServerLevelTickEvent>> g_tickListener;

} // namespace

[[nodiscard]] bool sendFrameToClient(NetworkIdentifier const& target, std::vector<std::uint8_t> const& frame) {
    if (frame.empty()) {
        return false;
    }
    SyncFramePacket packet(frame);
    packet.sendToClient(target, SubClientId::PrimaryClient);
    return true;
}

void serverLogInfo(std::string const& message) {
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info(message);
    }
}

void serverLogWarn(std::string const& message) {
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().warn(message);
    }
}

void serverLogError(std::string const& message) {
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().error(message);
    }
}

bool installServerPacketAdapter() {
    if (g_tickListener) {
        return true;
    }
    g_tickListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::world::ServerLevelTickEvent>(
        [](ll::event::world::ServerLevelTickEvent&) { SyncService::instance().tick(); }
    );
    if (!g_tickListener) {
        return false;
    }
    return true;
}

void uninstallServerPacketAdapter() noexcept {
    if (g_tickListener) {
        ll::event::EventBus::getInstance().removeListener<ll::event::world::ServerLevelTickEvent>(g_tickListener);
        g_tickListener.reset();
    }
}

} // namespace lholo::sync
