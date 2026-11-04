#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "sync/common/SharedTypes.h"

// Persistent server configuration (§8) and stable identity (§5.1 / §5.2).
// Config lives at <plugin-data>/LHoloSyncServer/config.json and the generated
// server/world UUIDs at <plugin-data>/LHoloSyncServer/server-id.json. The root
// comes from the LeviLamina mod data API, never a hard-coded "plugins/" path.

namespace lholo::sync {

struct ServerLimitsConfig {
    std::uint64_t maxFileMiB{128};
    std::uint64_t absoluteMaxFileMiB{256};
    std::uint32_t maxOwnerPlacements{20};
    std::uint32_t maxTotalPlacements{512};
    std::uint64_t maxTotalStoreMiB{2048};
    std::uint64_t maxStagingMiB{1024};
    std::uint32_t maxTransfersPerUser{2};
    std::uint32_t maxTotalTransfers{8};
    std::uint32_t userBandwidthMiBps{2};
    std::uint32_t serverBandwidthMiBps{8};
};

struct ServerTransferConfig {
    std::uint32_t chunkBytes{49152};
    std::uint32_t window{4};
    std::uint32_t idleTimeoutSeconds{60};
    std::uint32_t partialTtlHours{24};
};

struct ServerConfig {
    std::int32_t schemaVersion{1};
    bool enabled{true};
    std::string defaultRole{"viewer"};
    bool allowAuthenticatedUploads{false};
    bool operatorIsAdmin{true};
    ServerLimitsConfig limits{};
    ServerTransferConfig transfer{};
    std::uint32_t editLockSeconds{120};
    bool loggingAudit{true};
    bool loggingDebug{false};

    // Loads config.json from `configDir`, creating it with defaults when absent.
    // Invalid values are clamped to the protocol ceiling rather than trusted.
    [[nodiscard]] static ServerConfig loadOrCreate(std::filesystem::path const& configDir, std::string& error);
};

struct ServerIdentity {
    Uuid serverId{};
    Uuid worldId{};

    // Loads server-id.json from `dataDir`, generating stable UUIDs on first run
    // or when the file is missing/invalid.
    [[nodiscard]] static ServerIdentity loadOrCreate(std::filesystem::path const& dataDir, std::string& error);
};

} // namespace lholo::sync
