#include "ServerConfig.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "sync/common/Protocol.h"
#include "sync/common/Validation.h"

namespace lholo::sync {
namespace {

using nlohmann::json;

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

std::uint64_t clampU64(std::uint64_t value, std::uint64_t lo, std::uint64_t hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

bool writeTextFileAtomic(std::filesystem::path const& path, std::string const& text, std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot open " + temp.string();
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            error = "cannot write " + temp.string();
            return false;
        }
    }
    std::filesystem::remove(path, code);
    std::filesystem::rename(temp, path, code);
    if (code) {
        error = "cannot publish " + path.string() + ": " + code.message();
        return false;
    }
    return true;
}

json defaultConfigJson() {
    ServerConfig defaults;
    json limits = json::object();
    limits["max_file_mib"] = defaults.limits.maxFileMiB;
    limits["absolute_max_file_mib"] = defaults.limits.absoluteMaxFileMiB;
    limits["max_owner_placements"] = defaults.limits.maxOwnerPlacements;
    limits["max_total_placements"] = defaults.limits.maxTotalPlacements;
    limits["max_total_store_mib"] = defaults.limits.maxTotalStoreMiB;
    limits["max_staging_mib"] = defaults.limits.maxStagingMiB;
    limits["max_transfers_per_user"] = defaults.limits.maxTransfersPerUser;
    limits["max_total_transfers"] = defaults.limits.maxTotalTransfers;
    limits["user_bandwidth_mib_s"] = defaults.limits.userBandwidthMiBps;
    limits["server_bandwidth_mib_s"] = defaults.limits.serverBandwidthMiBps;

    json transfer = json::object();
    transfer["chunk_bytes"] = defaults.transfer.chunkBytes;
    transfer["window"] = defaults.transfer.window;
    transfer["idle_timeout_seconds"] = defaults.transfer.idleTimeoutSeconds;
    transfer["partial_ttl_hours"] = defaults.transfer.partialTtlHours;

    json permissions = json::object();
    permissions["default_role"] = defaults.defaultRole;
    permissions["allow_authenticated_uploads"] = defaults.allowAuthenticatedUploads;
    permissions["operator_is_admin"] = defaults.operatorIsAdmin;

    json logging = json::object();
    logging["audit"] = defaults.loggingAudit;
    logging["debug"] = defaults.loggingDebug;

    json root = json::object();
    root["schema_version"] = defaults.schemaVersion;
    root["enabled"] = defaults.enabled;
    root["permissions"] = std::move(permissions);
    root["limits"] = std::move(limits);
    root["transfer"] = std::move(transfer);
    root["edit_lock_seconds"] = defaults.editLockSeconds;
    root["logging"] = std::move(logging);
    return root;
}

} // namespace

ServerConfig ServerConfig::loadOrCreate(std::filesystem::path const& configDir, std::string& error) {
    ServerConfig config;
    std::error_code code;
    std::filesystem::create_directories(configDir, code);

    std::filesystem::path const path = configDir / "config.json";
    bool writeDefaults = true;
    if (std::filesystem::exists(path, code)) {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            try {
                json root = json::parse(in, nullptr, false);
                if (root.is_object()) {
                    writeDefaults = false;
                    if (root.contains("enabled") && root["enabled"].is_boolean()) {
                        config.enabled = root["enabled"].get<bool>();
                    }
                    if (root.contains("edit_lock_seconds") && root["edit_lock_seconds"].is_number_unsigned()) {
                        config.editLockSeconds = root["edit_lock_seconds"].get<std::uint32_t>();
                    }
                    if (auto permissions = root.find("permissions"); permissions != root.end() && permissions->is_object()) {
                        auto const& value = *permissions;
                        if (value.contains("default_role") && value["default_role"].is_string()) {
                            Role role;
                            if (parseRole(value["default_role"].get<std::string>(), role)) {
                                config.defaultRole = value["default_role"].get<std::string>();
                            }
                        }
                        if (value.contains("allow_authenticated_uploads") && value["allow_authenticated_uploads"].is_boolean()) {
                            config.allowAuthenticatedUploads = value["allow_authenticated_uploads"].get<bool>();
                        }
                        if (value.contains("operator_is_admin") && value["operator_is_admin"].is_boolean()) {
                            config.operatorIsAdmin = value["operator_is_admin"].get<bool>();
                        }
                    }
                    if (auto limits = root.find("limits"); limits != root.end() && limits->is_object()) {
                        auto const& value = *limits;
                        auto u64 = [&](char const* key, std::uint64_t fallback) -> std::uint64_t {
                            auto it = value.find(key);
                            return (it != value.end() && it->is_number_unsigned()) ? it->get<std::uint64_t>() : fallback;
                        };
                        config.limits.maxFileMiB = u64("max_file_mib", config.limits.maxFileMiB);
                        config.limits.absoluteMaxFileMiB = u64("absolute_max_file_mib", config.limits.absoluteMaxFileMiB);
                        config.limits.maxTotalStoreMiB = u64("max_total_store_mib", config.limits.maxTotalStoreMiB);
                        config.limits.maxStagingMiB = u64("max_staging_mib", config.limits.maxStagingMiB);
                    }
                    if (auto transfer = root.find("transfer"); transfer != root.end() && transfer->is_object()) {
                        auto const& value = *transfer;
                        auto u32 = [&](char const* key, std::uint32_t fallback) -> std::uint32_t {
                            auto it = value.find(key);
                            return (it != value.end() && it->is_number_unsigned()) ? it->get<std::uint32_t>() : fallback;
                        };
                        config.transfer.chunkBytes = u32("chunk_bytes", config.transfer.chunkBytes);
                        config.transfer.window = u32("window", config.transfer.window);
                        config.transfer.partialTtlHours = u32("partial_ttl_hours", config.transfer.partialTtlHours);
                    }
                }
            } catch (std::exception const& exception) {
                error = std::string{"config.json parse error: "} + exception.what();
            }
        } else {
            error = "cannot open " + path.string();
        }
    }

    // Clamp to the protocol's fixed ceilings; admin config may lower but not
    // exceed them (§8: administrator settings never exceed protocol limits).
    config.limits.absoluteMaxFileMiB = clampU64(config.limits.absoluteMaxFileMiB, 1, kAbsoluteMaxFileBytes / kMiB);
    config.limits.maxFileMiB = clampU64(config.limits.maxFileMiB, 1, config.limits.absoluteMaxFileMiB);
    config.transfer.chunkBytes = static_cast<std::uint32_t>(
        clampU64(config.transfer.chunkBytes, 1, kMaxFileChunkBytes)
    );
    config.transfer.window = static_cast<std::uint32_t>(clampU64(config.transfer.window, 1, 64));
    config.editLockSeconds = static_cast<std::uint32_t>(clampU64(config.editLockSeconds, 10, 3600));

    if (writeDefaults) {
        std::string writeError;
        if (!writeTextFileAtomic(path, defaultConfigJson().dump(2), writeError)) {
            error = writeError;
        }
    }
    return config;
}

ServerIdentity ServerIdentity::loadOrCreate(std::filesystem::path const& dataDir, std::string& error) {
    ServerIdentity identity;
    std::error_code code;
    std::filesystem::create_directories(dataDir, code);

    std::filesystem::path const path = dataDir / "server-id.json";
    bool valid = false;
    if (std::filesystem::exists(path, code)) {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            try {
                json root = json::parse(in, nullptr, false);
                if (root.is_object()) {
                    std::string serverId;
                    std::string worldId;
                    if (root.contains("server_id") && root["server_id"].is_string()) {
                        serverId = root["server_id"].get<std::string>();
                    }
                    if (root.contains("world_id") && root["world_id"].is_string()) {
                        worldId = root["world_id"].get<std::string>();
                    }
                    valid = Uuid::parse(serverId, identity.serverId) && Uuid::parse(worldId, identity.worldId);
                }
            } catch (std::exception const& exception) {
                error = std::string{"server-id.json parse error: "} + exception.what();
            }
        }
    }

    if (!valid) {
        identity.serverId = Uuid::random();
        identity.worldId = Uuid::random();
        json root = json::object();
        root["server_id"] = identity.serverId.toString();
        root["world_id"] = identity.worldId.toString();
        std::string writeError;
        if (!writeTextFileAtomic(path, root.dump(2), writeError)) {
            error = writeError;
        }
    }
    return identity;
}

} // namespace lholo::sync
