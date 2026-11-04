#include "FileStore.h"

#include <array>
#include <fstream>
#include <random>

#include "sync/common/PathSupport.h"
#include "sync/common/Sha256.h"

namespace lholo::sync {
namespace {

constexpr std::size_t kHashChunk = 1u << 16; // 64 KiB (kept well under the default stack)

std::string extensionFor(SchematicFormat format) {
    switch (format) {
        case SchematicFormat::Litematic: return ".litematic";
        case SchematicFormat::Mcstructure: return ".mcstructure";
        case SchematicFormat::Unknown: break;
    }
    return ".bin";
}

} // namespace

FileStore::FileStore(std::filesystem::path objectsRoot, std::filesystem::path stagingRoot, FileStoreLimits limits)
: mObjectsRoot(std::move(objectsRoot)),
  mStagingRoot(std::move(stagingRoot)),
  mLimits(limits) {
    std::error_code code;
    std::filesystem::create_directories(paths::ioPath(mObjectsRoot), code);
    std::filesystem::create_directories(paths::ioPath(mStagingRoot), code);
}

std::filesystem::path FileStore::objectPath(Sha256Hash const& hash, SchematicFormat format) const {
    std::string const hex = hash.toHex();
    return mObjectsRoot / hex.substr(0, 2) / (hex + extensionFor(format));
}

bool FileStore::hasObject(Sha256Hash const& hash, SchematicFormat format, std::uint64_t sizeBytes) const {
    std::error_code code;
    auto const path = paths::ioPath(objectPath(hash, format));
    if (!std::filesystem::is_regular_file(path, code)) {
        return false;
    }
    auto const size = std::filesystem::file_size(path, code);
    return !code && size == sizeBytes;
}

std::optional<std::filesystem::path> FileStore::createStagingFile(std::string& error) const {
    std::error_code code;
    std::filesystem::create_directories(paths::ioPath(mStagingRoot), code);
    std::random_device device;
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::filesystem::path candidate =
            mStagingRoot / ("xfer-" + std::to_string(device()) + std::to_string(device()) + ".part");
        auto const io = paths::ioPath(candidate);
        if (!std::filesystem::exists(io, code)) {
            std::ofstream touch(io, std::ios::binary | std::ios::trunc);
            if (!touch) {
                error = "cannot create staging file " + candidate.string();
                return std::nullopt;
            }
            return candidate;
        }
    }
    error = "cannot allocate a unique staging file name";
    return std::nullopt;
}

std::optional<Sha256Hash> FileStore::hashFile(std::filesystem::path const& path, std::string& error) {
    std::ifstream in(paths::ioPath(path), std::ios::binary);
    if (!in) {
        error = "cannot open " + path.string();
        return std::nullopt;
    }
    sha256::Hasher hasher;
    std::array<char, kHashChunk> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize const got = in.gcount();
        if (got > 0) {
            hasher.update(buffer.data(), static_cast<std::size_t>(got));
        }
    }
    if (in.bad()) {
        error = "read error in " + path.string();
        return std::nullopt;
    }
    return hasher.finish();
}

std::optional<std::filesystem::path> FileStore::publish(
    std::filesystem::path const& sourcePath,
    Sha256Hash const& hash,
    SchematicFormat format,
    std::uint64_t sizeBytes,
    std::string& error
) {
    if (sizeBytes == 0) {
        error = "empty object rejected";
        return std::nullopt;
    }
    if (sizeBytes > mLimits.maxFileBytes) {
        error = "object exceeds configured file limit";
        return std::nullopt;
    }

    std::error_code code;
    auto const ioSource = paths::ioPath(sourcePath);
    if (!std::filesystem::is_regular_file(ioSource, code)) {
        error = "source file missing: " + sourcePath.string();
        return std::nullopt;
    }
    auto const actualSize = std::filesystem::file_size(ioSource, code);
    if (code || actualSize != sizeBytes) {
        error = "source size does not match declared size";
        return std::nullopt;
    }

    auto const destination = objectPath(hash, format);
    if (hasObject(hash, format, sizeBytes)) {
        return destination; // already stored: deduplicate
    }

    // Verify the source bytes before publishing (§4.7 FILE_COMPLETE semantics).
    auto const actualHash = hashFile(sourcePath, error);
    if (!actualHash) {
        return std::nullopt;
    }
    if (*actualHash != hash) {
        error = "sha256 mismatch while publishing object";
        return std::nullopt;
    }

    std::filesystem::create_directories(paths::ioPath(destination.parent_path()), code);

    // Stage next to the destination so the final rename is atomic on one volume.
    std::filesystem::path staged = destination;
    staged += ".staged";
    auto const ioStaged = paths::ioPath(staged);
    std::filesystem::remove(ioStaged, code);
    std::filesystem::copy_file(ioSource, ioStaged, std::filesystem::copy_options::overwrite_existing, code);
    if (code) {
        error = "cannot stage object: " + code.message();
        std::filesystem::remove(ioStaged, code);
        return std::nullopt;
    }

    // Re-verify the staged copy in case of a torn copy.
    auto const stagedHash = hashFile(staged, error);
    if (!stagedHash || *stagedHash != hash) {
        error = "staged object failed hash verification";
        std::filesystem::remove(ioStaged, code);
        return std::nullopt;
    }

    // Another concurrent publisher may have won the race; losing the rename to
    // an existing identical object is still success.
    if (hasObject(hash, format, sizeBytes)) {
        std::filesystem::remove(ioStaged, code);
        return destination;
    }
    auto const ioDestination = paths::ioPath(destination);
    std::filesystem::rename(ioStaged, ioDestination, code);
    if (code) {
        std::filesystem::remove(ioDestination, code);
        code.clear();
        std::filesystem::rename(ioStaged, ioDestination, code);
        if (code) {
            error = "cannot publish object: " + code.message();
            std::filesystem::remove(ioStaged, code);
            return std::nullopt;
        }
    }
    return destination;
}

std::uint64_t FileStore::objectCount() const {
    std::error_code code;
    std::uint64_t count = 0;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(paths::ioPath(mObjectsRoot), code)) {
        if (entry.is_regular_file()) {
            ++count;
        }
    }
    return count;
}

std::uint64_t FileStore::totalObjectBytes() const {
    std::error_code code;
    std::uint64_t total = 0;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(paths::ioPath(mObjectsRoot), code)) {
        if (entry.is_regular_file()) {
            total += static_cast<std::uint64_t>(entry.file_size(code));
        }
    }
    return total;
}

bool FileStore::removeObject(Sha256Hash const& hash, SchematicFormat format) {
    std::error_code code;
    auto const path = paths::ioPath(objectPath(hash, format));
    if (!std::filesystem::is_regular_file(path, code)) {
        return false;
    }
    return std::filesystem::remove(path, code) && !code;
}

} // namespace lholo::sync
