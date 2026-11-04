#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "sync/common/Protocol.h"
#include "sync/common/SharedTypes.h"

// Content-addressed object store (§5.2). Objects live at
//   <objectsRoot>/<first two hex>/<sha256>.<ext>
// The first two hash characters fan out the directory. Publication is atomic:
// bytes are first written to a staging file, then verified and renamed into
// place, so a partially transferred object is never visible to readers.

namespace lholo::sync {

struct FileStoreLimits {
    std::uint64_t maxFileBytes{kDefaultMaxFileBytes};
    std::uint64_t maxStagingBytes{1024ull * 1024 * 1024};
};

class FileStore {
public:
    FileStore(std::filesystem::path objectsRoot, std::filesystem::path stagingRoot, FileStoreLimits limits);

    [[nodiscard]] std::filesystem::path const& objectsRoot() const { return mObjectsRoot; }
    [[nodiscard]] std::filesystem::path const& stagingRoot() const { return mStagingRoot; }

    [[nodiscard]] std::filesystem::path objectPath(Sha256Hash const& hash, SchematicFormat format) const;

    // True when a stored object with this hash/format exists and has `sizeBytes`.
    [[nodiscard]] bool hasObject(Sha256Hash const& hash, SchematicFormat format, std::uint64_t sizeBytes) const;

    // Publishes `sourcePath` as the object for (hash, format). Deduplicates: if
    // an identical object already exists it is left untouched. Verifies size and
    // SHA-256 of the source bytes before publishing. Returns the destination
    // path on success.
    [[nodiscard]] std::optional<std::filesystem::path> publish(
        std::filesystem::path const& sourcePath,
        Sha256Hash const& hash,
        SchematicFormat format,
        std::uint64_t sizeBytes,
        std::string& error
    );

    // Writes `bytes` to a uniquely named staging file and returns its path.
    [[nodiscard]] std::optional<std::filesystem::path> createStagingFile(std::string& error) const;

    // Streams a file and returns its SHA-256 (bounds: file must exist).
    [[nodiscard]] static std::optional<Sha256Hash> hashFile(std::filesystem::path const& path, std::string& error);

    [[nodiscard]] std::uint64_t objectCount() const;

    // Total bytes of all published objects (used for storage quota checks).
    [[nodiscard]] std::uint64_t totalObjectBytes() const;

    // Removes one stored object. Only used by administrator GC after the caller
    // has verified no active placement references it.
    bool removeObject(Sha256Hash const& hash, SchematicFormat format);

private:
    std::filesystem::path mObjectsRoot;
    std::filesystem::path mStagingRoot;
    FileStoreLimits mLimits;
};

} // namespace lholo::sync
