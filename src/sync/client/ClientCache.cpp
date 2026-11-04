#include "sync/client/ClientCache.h"

#include <array>
#include <fstream>

#include "sync/common/PathSupport.h"
#include "sync/common/Sha256.h"

namespace lholo::sync {
namespace {

constexpr std::size_t kVerifyChunk = 1u << 16; // 64 KiB

std::string extensionFor(SchematicFormat format) {
    switch (format) {
        case SchematicFormat::Litematic: return ".litematic";
        case SchematicFormat::Mcstructure: return ".mcstructure";
        case SchematicFormat::Unknown: break;
    }
    return ".bin";
}

std::optional<Sha256Hash> hashFile(std::filesystem::path const& path) {
    std::ifstream in(paths::ioPath(path), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    sha256::Hasher hasher;
    std::array<char, kVerifyChunk> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize const got = in.gcount();
        if (got > 0) {
            hasher.update(buffer.data(), static_cast<std::size_t>(got));
        }
    }
    if (in.bad()) {
        return std::nullopt;
    }
    return hasher.finish();
}

// Cache paths are built from hex hashes and canonical UUID strings only, so no
// component can contain a path separator or a traversal segment.
bool isSafeComponent(std::string_view text) {
    if (text.empty() || text == "." || text == "..") {
        return false;
    }
    for (char const c : text) {
        bool const ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace

ClientCache::ClientCache(std::filesystem::path schematicsRoot) : mRoot(std::move(schematicsRoot)) {}

std::string ClientCache::endpointKey(std::string const& address, std::uint16_t port) {
    std::string const normalized = address + ":" + std::to_string(port);
    auto const hash = sha256::hashString(normalized).toHex();
    return hash.substr(0, 16);
}

std::filesystem::path ClientCache::worldRoot(
    std::string const& endpointKey,
    Uuid const& serverId,
    Uuid const& worldId
) const {
    return mRoot / "sync" / endpointKey / serverId.toString() / worldId.toString();
}

bool ClientCache::contained(std::filesystem::path const& path) const {
    return paths::lexicallyContained(mRoot, path);
}

std::filesystem::path ClientCache::partPath(
    std::string const& endpointKey,
    Uuid const& serverId,
    Uuid const& worldId,
    Sha256Hash const& hash,
    SchematicFormat format
) const {
    if (!isSafeComponent(endpointKey) || serverId.isNil() || worldId.isNil()) {
        return {};
    }
    return worldRoot(endpointKey, serverId, worldId) / (hash.toHex() + extensionFor(format) + ".part");
}

std::optional<std::filesystem::path> ClientCache::findVerified(
    std::string const& endpointKey,
    Uuid const& serverId,
    Uuid const& worldId,
    Sha256Hash const& hash,
    SchematicFormat format,
    std::uint64_t sizeBytes
) {
    if (!isSafeComponent(endpointKey) || hash.isZero() || serverId.isNil() || worldId.isNil()) {
        return std::nullopt;
    }
    auto const path = worldRoot(endpointKey, serverId, worldId) / (hash.toHex() + extensionFor(format));
    if (!contained(path)) {
        return std::nullopt;
    }
    std::error_code code;
    auto const io = paths::ioPath(path);
    if (!std::filesystem::is_regular_file(io, code)) {
        return std::nullopt;
    }
    auto const size = std::filesystem::file_size(io, code);
    if (code) {
        return std::nullopt;
    }
    // sizeBytes == 0 means "size unknown" (the catalog carries only the hash);
    // the SHA-256 check below is still authoritative.
    if (sizeBytes != 0 && size != sizeBytes) {
        return std::nullopt;
    }
    // Verify the bytes on first reference too: a filename alone is never proof
    // (§5.4).
    auto const actual = hashFile(path);
    if (!actual || *actual != hash) {
        return std::nullopt;
    }
    return path;
}

std::optional<std::filesystem::path> ClientCache::publishPart(
    std::string const& endpointKey,
    Uuid const& serverId,
    Uuid const& worldId,
    Sha256Hash const& hash,
    SchematicFormat format,
    std::uint64_t sizeBytes,
    std::string& error
) {
    if (sizeBytes == 0) {
        error = "empty object rejected";
        return std::nullopt;
    }
    if (!isSafeComponent(endpointKey) || hash.isZero() || serverId.isNil() || worldId.isNil()) {
        error = "invalid cache key";
        return std::nullopt;
    }
    auto const part = partPath(endpointKey, serverId, worldId, hash, format);
    if (part.empty() || !contained(part)) {
        error = "cache path escapes root";
        return std::nullopt;
    }
    std::error_code code;
    auto const ioPart = paths::ioPath(part);
    if (!std::filesystem::is_regular_file(ioPart, code)) {
        error = "missing .part file";
        return std::nullopt;
    }
    auto const partSize = std::filesystem::file_size(ioPart, code);
    if (code || partSize != sizeBytes) {
        error = "part size mismatch";
        return std::nullopt;
    }
    auto const actual = hashFile(part);
    if (!actual || *actual != hash) {
        error = "part sha256 mismatch";
        return std::nullopt;
    }

    auto const destination = worldRoot(endpointKey, serverId, worldId) / (hash.toHex() + extensionFor(format));
    if (!contained(destination)) {
        error = "cache path escapes root";
        return std::nullopt;
    }
    auto const ioDestination = paths::ioPath(destination);
    // Preserve an existing valid object and discard the duplicate download.
    if (std::filesystem::is_regular_file(ioDestination, code)) {
        auto const existing = std::filesystem::file_size(ioDestination, code);
        auto const existingHash = hashFile(destination);
        if (!code && existing == sizeBytes && existingHash && *existingHash == hash) {
            std::filesystem::remove(ioPart, code);
            return destination;
        }
    }
    std::filesystem::create_directories(paths::ioPath(destination.parent_path()), code);
    std::filesystem::remove(ioDestination, code); // only our own cache object
    code.clear();
    std::filesystem::rename(ioPart, ioDestination, code);
    if (code) {
        error = "cannot publish cache object: " + code.message();
        return std::nullopt;
    }
    return destination;
}

bool ClientCache::removeCached(
    std::string const& endpointKey,
    Uuid const& serverId,
    Uuid const& worldId,
    Sha256Hash const& hash,
    SchematicFormat format
) {
    if (!isSafeComponent(endpointKey) || hash.isZero()) {
        return false;
    }
    std::error_code code;
    auto const path = worldRoot(endpointKey, serverId, worldId) / (hash.toHex() + extensionFor(format));
    if (!contained(path)) {
        return false;
    }
    return std::filesystem::remove(paths::ioPath(path), code) && !code;
}

} // namespace lholo::sync
