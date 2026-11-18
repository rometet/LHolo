#include "AdminCommands.h"

#include <string>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/Optional.h"
#include "ll/api/mod/NativeMod.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"

#include "ServerConfig.h"
#include "SyncService.h"

namespace lholo::sync {

// Command parameter types must have external linkage (boost::pfr reflects them).
struct NoParams {};

struct UuidParam {
    std::string value;
};

struct RoleParam {
    std::string player;
    std::string role;
};

struct GcParam {
    std::string mode;
};

bool registerAdminCommands() {
    auto& cmd = ll::command::CommandRegistrar::getServerInstance().getOrCreateCommand(
        "lholosync",
        "LHolo Integrated Sync administration",
        CommandPermissionLevel::GameDirectors
    );

    cmd.overload<NoParams>().text("status").execute(
        [](CommandOrigin const&, CommandOutput& output, NoParams const&) {
            output.success(SyncService::instance().adminStatusText());
        }
    );

    cmd.overload<UuidParam>().text("delete").required("value").execute(
        [](CommandOrigin const&, CommandOutput& output, UuidParam const& param) {
            Uuid id;
            if (!Uuid::parse(param.value, id)) {
                output.error("invalid placement uuid");
                return;
            }
            std::string message;
            if (SyncService::instance().adminDelete(id, message)) {
                output.success(message);
            } else {
                output.error(message);
            }
        }
    );

    cmd.overload<UuidParam>().text("unlock").required("value").execute(
        [](CommandOrigin const&, CommandOutput& output, UuidParam const& param) {
            Uuid id;
            if (!Uuid::parse(param.value, id)) {
                output.error("invalid placement uuid");
                return;
            }
            std::string message;
            SyncService::instance().adminUnlock(id, message);
            output.success(message);
        }
    );

    cmd.overload<RoleParam>().text("role").required("player").required("role").execute(
        [](CommandOrigin const&, CommandOutput& output, RoleParam const& param) {
            std::string message;
            if (SyncService::instance().adminSetRole(param.player, param.role, message)) {
                output.success(message);
            } else {
                output.error(message);
            }
        }
    );

    cmd.overload<GcParam>().text("gc").required("mode").execute(
        [](CommandOrigin const&, CommandOutput& output, GcParam const& param) {
            std::string message;
            if (SyncService::instance().adminGc(param.mode, message)) {
                output.success(message);
            } else {
                output.error(message);
            }
        }
    );

    cmd.overload<NoParams>().text("reload").execute(
        [](CommandOrigin const&, CommandOutput& output, NoParams const&) {
            auto mod = ll::mod::NativeMod::current();
            if (!mod) {
                output.error("mod context unavailable");
                return;
            }
            std::string error;
            ServerConfig const config = ServerConfig::loadOrCreate(mod->getConfigDir(), error);
            SyncService::instance().applyConfig(config);
            if (error.empty()) {
                output.success("config reloaded");
            } else {
                output.success("config reloaded with warning: " + error);
            }
        }
    );

    return true;
}

} // namespace lholo::sync
