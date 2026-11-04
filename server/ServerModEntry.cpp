#include <string_view>

#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

#include "ServerConfig.h"
#include "AdminCommands.h"
#include "ServerPacketAdapter.h"
#include "SyncService.h"
#include "PlayerIdentity.h"

// LHoloSyncServer — dedicated-server companion plugin for LHolo Integrated Sync.
// Installed only on the BDS server; ordinary clients only update LHolo.dll.

namespace lholo::sync {

inline constexpr std::string_view kServerModVersion = "26.51.5-1.0.0";

class LHoloSyncServer {
public:
    static LHoloSyncServer& getInstance();

    LHoloSyncServer(LHoloSyncServer const&)            = delete;
    LHoloSyncServer(LHoloSyncServer&&)                 = delete;
    LHoloSyncServer& operator=(LHoloSyncServer const&) = delete;
    LHoloSyncServer& operator=(LHoloSyncServer&&)      = delete;

    bool load();
    bool enable();
    bool disable();
    bool unload();

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

private:
    LHoloSyncServer() : mSelf(*ll::mod::NativeMod::current()) {}

    ll::mod::NativeMod& mSelf;
};

LHoloSyncServer& LHoloSyncServer::getInstance() {
    static LHoloSyncServer instance;
    return instance;
}

bool LHoloSyncServer::load() {
    return true;
}

bool LHoloSyncServer::enable() {
    auto& mod = getSelf();
    std::string error;

    ServerConfig config = ServerConfig::loadOrCreate(mod.getConfigDir(), error);
    if (!error.empty()) {
        serverLogWarn("LHoloSyncServer: " + error + " (using defaults)");
        error.clear();
    }
    if (!config.enabled) {
        serverLogInfo("LHoloSyncServer: disabled by config; sync features are inactive");
        return true;
    }

    ServerIdentity identity = ServerIdentity::loadOrCreate(mod.getDataDir(), error);
    if (!error.empty()) {
        serverLogWarn("LHoloSyncServer: " + error + " (using generated identity)");
        error.clear();
    }

    if (!SyncService::instance().start(std::string(kServerModVersion), identity, config, mod.getDataDir())) {
        serverLogError("LHoloSyncServer: failed to start sync service");
        return false;
    }
    // Track stable, server-authenticated player identities (XUID) for ownership.
    if (!PlayerIdentityResolver::instance().install()) {
        serverLogWarn("LHoloSyncServer: player identity listeners failed to install");
    }
    if (!installServerPacketAdapter()) {
        serverLogError("LHoloSyncServer: failed to install packet adapter");
        SyncService::instance().stop();
        return false;
    }
    if (!registerAdminCommands()) {
        serverLogWarn("LHoloSyncServer: /lholosync command registration failed");
    }
    serverLogInfo("LHoloSyncServer enabled (version " + std::string(kServerModVersion) + ")");
    return true;
}

bool LHoloSyncServer::disable() {
    PlayerIdentityResolver::instance().uninstall();
    uninstallServerPacketAdapter();
    SyncService::instance().stop();
    serverLogInfo("LHoloSyncServer disabled");
    return true;
}

bool LHoloSyncServer::unload() {
    return true;
}

} // namespace lholo::sync

LL_REGISTER_MOD(lholo::sync::LHoloSyncServer, lholo::sync::LHoloSyncServer::getInstance());
