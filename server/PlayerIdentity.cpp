#include "PlayerIdentity.h"

#include <memory>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/common/SubClientId.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/server/ServerPlayer.h"

namespace lholo::sync {
namespace {

std::shared_ptr<ll::event::Listener<ll::event::player::PlayerJoinEvent>> g_join;
std::shared_ptr<ll::event::Listener<ll::event::player::PlayerDisconnectEvent>> g_leave;

} // namespace

std::string PlayerIdentity::actorKey() const {
    return xuid.empty() ? std::string{} : (std::string{"xuid:"} + xuid);
}

PlayerIdentityResolver& PlayerIdentityResolver::instance() {
    static PlayerIdentityResolver resolver;
    return resolver;
}
bool PlayerIdentityResolver::install() {
    auto& bus = ll::event::EventBus::getInstance();
    if (!g_join) {
        g_join = bus.emplaceListener<ll::event::player::PlayerJoinEvent>(
            [this](ll::event::player::PlayerJoinEvent& event) {
                auto& player = event.self();
                PlayerIdentity id;
                id.xuid = player.getXuid();
                id.isOperator = player.isOperator();
                std::lock_guard lock{mMutex};
                mCache[player.getNetworkIdentifier()] = id;
            }
        );
    }
    if (!g_leave) {
        g_leave = bus.emplaceListener<ll::event::player::PlayerDisconnectEvent>(
            [this](ll::event::player::PlayerDisconnectEvent& event) {
                auto& player = event.self();
                erase(player.getNetworkIdentifier());
            }
        );
    }
    return g_join && g_leave;
}
void PlayerIdentityResolver::uninstall() noexcept {
    auto& bus = ll::event::EventBus::getInstance();
    if (g_join) {
        bus.removeListener<ll::event::player::PlayerJoinEvent>(g_join);
        g_join.reset();
    }
    if (g_leave) {
        bus.removeListener<ll::event::player::PlayerDisconnectEvent>(g_leave);
        g_leave.reset();
    }
    std::lock_guard lock{mMutex};
    mCache.clear();
}

std::optional<PlayerIdentity> PlayerIdentityResolver::resolve(NetworkIdentifier const& source) {
    {
        std::lock_guard lock{mMutex};
        auto it = mCache.find(source);
        if (it != mCache.end() && it->second.authenticated()) {
            return it->second;
        }
    }
    if (auto handler = ll::service::getServerNetworkHandler()) {
        if (auto* player = handler->_getServerPlayer(source, SubClientId::PrimaryClient)) {
            PlayerIdentity id;
            id.xuid = player->getXuid();
            id.isOperator = player->isOperator();
            std::lock_guard lock{mMutex};
            mCache[source] = id;
            if (!id.authenticated()) {
                return std::nullopt;
            }
            return id;
        }
    }
    return std::nullopt;
}

void PlayerIdentityResolver::erase(NetworkIdentifier const& source) {
    std::lock_guard lock{mMutex};
    mCache.erase(source);
}

} // namespace lholo::sync