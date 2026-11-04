#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "sync/common/Protocol.h"
#include "sync/common/SharedTypes.h"

// Client-side content cache (§5.4). Objects live under
//   <schematicsRoot>/sync/<endpoint-hash>/<server-uuid>/<world-uuid>/
//       <sha256>.<ext>
// and a sibling `*.part` file is used for in-flight downloads. Bytes are only
// published after the size and SHA-256 are verified, never overwriting an
// existing valid object, and every emitted path is checked to remain inside the
// cache root (no symlink/junction/path escape).

namespace lholo::sync {

class ClientCache {
public:
    explicit ClientCache(std::filesystem::path schematicsRoot);

    // Stable 16-hex prefix of SHA-256("address:port") — derived from the
    // endpoint the client actually connected to, never a server-declared value.
    [[nodiscard]] static std::string endpointKey(std::string const& address, std::uint16_t port);

    [[nodiscard]] std::filesystem::path worldRoot(
        std::string const& endpointKey,
        Uuid const& serverId,
        Uuid const& worldId
    ) const;

    [[nodiscard]] std::optional<std::filesystem::path> findVerified(
        std::string const& endpointKey,
        Uuid const& serverId,
        Uuid const& worldId,
        Sha256Hash const& hash,
        SchematicFormat format,
        std::uint64_t sizeBytes
    );

    [[nodiscard]] std::filesystem::path partPath(
        std::string const& endpointKey,
        Uuid const& serverId,
        Uuid const& worldId,
        Sha256Hash const& hash,
        SchematicFormat format
    ) const;

    // Verifies the part file (size + SHA-256) and atomically publishes it.
    [[nodiscard]] std::optional<std::filesystem::path> publishPart(
        std::string const& endpointKey,
        Uuid const& serverId,
        Uuid const& worldId,
        Sha256Hash const& hash,
        SchematicFormat format,
        std::uint64_t sizeBytes,
        std::string& error
    );

    // Removes only cache-owned objects for this server/world.
    bool removeCached(
        std::string const& endpointKey,
        Uuid const& serverId,
        Uuid const& worldId,
        Sha256Hash const& hash,
        SchematicFormat format
    );

    [[nodiscard]] std::filesystem::path const& schematicsRoot() const { return mRoot; }

private:
    // Validates that `path` stays within the cache root; returns false on escape.
    [[nodiscard]] bool contained(std::filesystem::path const& path) const;

    std::filesystem::path mRoot;
};

} // namespace lholo::sync
