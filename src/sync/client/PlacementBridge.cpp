#include "sync/client/PlacementBridge.h"

#include <array>
#include <filesystem>
#include <fstream>

#include "structure/PlacementSession.h"
#include "structure/SchematicRuntime.h"
#include "structure/StructurePaths.h"

#include "sync/client/FriendlySchematicName.h"
#include "sync/common/PathSupport.h"
#include "sync/common/Protocol.h"
#include "sync/common/Sha256.h"

namespace lholo::sync {
namespace {

// A friendly library name may collide with an unrelated user's schematic.
// Compare the bytes before reusing any path; never overwrite their file.
std::optional<Sha256Hash> hashLibraryFile(std::filesystem::path const& file) {
    std::ifstream input(paths::ioPath(file), std::ios::binary);
    if (!input) return std::nullopt;
    sha256::Hasher hasher;
    std::array<char, 65536> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto const size = input.gcount();
        if (size > 0) hasher.update(buffer.data(), static_cast<std::size_t>(size));
    }
    if (input.bad()) return std::nullopt;
    return hasher.finish();
}

// Applies the server-authoritative transform to a SavedPlacement without
// touching client-owned display fields (visibility, layers, name).
void applyServerTransform(structure::SavedPlacement& placement, SharedPlacementDto const& dto) {
    placement.dimension = dto.dimension;
    placement.origin = structure::Cell{dto.originX, dto.originY, dto.originZ};
    placement.rotation = dto.rotation;
    placement.mirror = dto.mirror;
}

} // namespace

bool PlacementBridge::importToLibrary(
    SharedPlacementDto const& placement,
    std::filesystem::path const& cachePath,
    std::string& libraryRoot,
    std::string& libraryRelative,
    std::string& error
) {
    auto const snap = structure::schematic::snapshot();
    if (snap.library.empty()) {
        error = "LHolo library is unavailable";
        return false;
    }
    libraryRoot = snap.library;
    // The hashed cache path remains private to synchronization. Names visible
    // in the LHolo library come from the shared placement's display name.
    // Prefer reusing an existing file with identical verified content.
    auto const extension = schematicExtension(placement.format);
    auto const friendly = friendlySchematicFilename(placement.displayName, placement.format, placement.fileHash);
    auto const stem = friendly.substr(0, friendly.size() - extension.size());
    auto const shortHash = placement.fileHash.toHex().substr(0, 12);
    auto const libraryPath = structure::detail::pathFromUtf8(libraryRoot);

    try {
        std::error_code code;
        std::filesystem::create_directories(paths::ioPath(libraryPath), code);
        if (code) {
            error = "cannot create schematic library: " + code.message();
            return false;
        }
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            std::string candidate = friendly;
            if (attempt != 0) {
                candidate = stem + "-" + shortHash;
                if (attempt > 1) candidate += "-" + std::to_string(attempt - 1);
                candidate += extension;
            }
            if (!structure::safeSchematicPath(candidate)) continue;
            auto const destination = structure::resolveSchematicPath(libraryPath, candidate);
            auto const native = paths::ioPath(destination);

            code.clear();
            bool const exists = std::filesystem::exists(native, code);
            if (code) {
                error = "cannot inspect library file: " + code.message();
                return false;
            }
            if (exists) {
                if (std::filesystem::is_regular_file(native, code)) {
                    auto const existingHash = hashLibraryFile(destination);
                    if (existingHash && *existingHash == placement.fileHash) {
                        libraryRelative = std::move(candidate);
                        return true;
                    }
                }
                continue; // same human name, different content: never overwrite
            }

            code.clear();
            if (std::filesystem::copy_file(
                    paths::ioPath(cachePath), native, std::filesystem::copy_options::none, code
                )) {
                libraryRelative = std::move(candidate);
                return true;
            }
            if (code == std::errc::file_exists) continue; // another writer won the race
            error = "cannot import shared file into the library: " + code.message();
            return false;
        }
    } catch (std::exception const& ex) {
        error = "cannot import shared file: ";
        error += ex.what();
        return false;
    }
    error = "no unused friendly schematic filename";
    return false;
}

PlacementBridge::Result PlacementBridge::activate(
    SharedPlacementDto const& placement,
    std::filesystem::path const& verifiedCachePath
) {
    Result result;
    auto const snap = structure::schematic::snapshot();
    if (!snap.worldAvailable) {
        result.status = "no world";
        return result;
    }

    std::string error;
    std::string libraryRoot;
    std::string libraryRelative;
    if (!importToLibrary(placement, verifiedCachePath, libraryRoot, libraryRelative, error)) {
        result.status = std::move(error);
        return result;
    }

    if (snap.dimension != placement.dimension) {
        mPending = Pending{placement, verifiedCachePath, libraryRelative};
        mBoundRemote = placement.placementId;
        mBoundFile.clear();
        mBoundPlacementId = 0;
        mAppliedRevision = 0;
        result.deferredDimension = true;
        result.status = "waiting for shared dimension";
        return result;
    }

    if (!activateAtSharedOrigin(placement, libraryRelative)) {
        result.status = "could not display schematic at the original shared position";
        return result;
    }
    mPending.reset();
    result.placed = true;
    result.status = "displayed at original shared position";
    return result;
}

bool PlacementBridge::activateAtSharedOrigin(
    SharedPlacementDto const& placement,
    std::string const& libraryRelative
) {
    auto const snap = structure::schematic::snapshot();
    if (!snap.worldAvailable || !snap.session.writable || snap.dimension != placement.dimension) return false;

    // Repeated clicks reuse the exact bound placement (not just any matching
    // filename), so they never create a duplicate at the player's feet.
    if (mBoundRemote && *mBoundRemote == placement.placementId
        && mBoundPlacementId != 0 && mBoundFile == libraryRelative) {
        for (auto const& current : snap.session.document.placements) {
            if (current.id != mBoundPlacementId || current.file != libraryRelative) continue;
            if (!structure::schematic::select(current.id)) return false;
            auto updated = current;
            applyServerTransform(updated, placement);
            auto const fresh = structure::schematic::snapshot();
            if (updated != current && !structure::schematic::edit(updated, fresh.session.revision)) return false;
            mAppliedRevision = placement.revision;
            return true;
        }
    }

    // The first renderer activation sees the shared world position. There is
    // no intermediate local-feet placement to correct asynchronously.
    if (!structure::schematic::placeAt(
            libraryRelative,
            structure::Cell{placement.originX, placement.originY, placement.originZ},
            placement.dimension, placement.rotation, placement.mirror
        )) return false;
    auto const after = structure::schematic::snapshot();
    auto const* selected = structure::selectedPlacement(after.session.document);
    if (!selected || selected->file != libraryRelative) return false;
    mBoundRemote = placement.placementId;
    mBoundFile = libraryRelative;
    mBoundPlacementId = selected->id;
    mAppliedRevision = placement.revision;
    return true;
}

void PlacementBridge::tick() {
    if (!mPending) return;
    auto const snap = structure::schematic::snapshot();
    if (!snap.worldAvailable || snap.dimension != mPending->placement.dimension) return;
    auto const pending = *mPending;
    if (activateAtSharedOrigin(pending.placement, pending.libraryRelative)) {
        mPending.reset();
    }
}

bool PlacementBridge::applyRemoteUpdate(SharedPlacementDto const& placement) {
    if (!mBoundRemote || !(*mBoundRemote == placement.placementId) || mBoundFile.empty()) {
        return false;
    }
    auto const snap = structure::schematic::snapshot();
    if (!snap.worldAvailable || !snap.session.writable) {
        return false;
    }
    for (auto const& current : snap.session.document.placements) {
        // Same file may be used by multiple unrelated local placements.
        if (current.id != mBoundPlacementId || current.file != mBoundFile) {
            continue;
        }
        if (snap.dimension != placement.dimension) {
            // Different dimension: defer rather than move the projection.
            mPending = Pending{placement, {}, mBoundFile};
            return true;
        }
        structure::SavedPlacement updated = current;
        applyServerTransform(updated, placement);
        if (updated != current) {
            if (!structure::schematic::edit(updated, snap.session.revision)) {
                return false;
            }
        }
        mAppliedRevision = placement.revision;
        return true;
    }
    return false;
}

void PlacementBridge::retire(Uuid const& remotePlacementId) {
    if (mPending && mPending->placement.placementId == remotePlacementId) {
        mPending.reset();
    }
    if (!mBoundRemote || !(*mBoundRemote == remotePlacementId)) {
        return;
    }
    if (mBoundPlacementId != 0 && !mBoundFile.empty()) {
        auto const snap = structure::schematic::snapshot();
        for (auto const& placement : snap.session.document.placements) {
            if (placement.id == mBoundPlacementId && placement.file == mBoundFile) {
                (void)structure::schematic::erase(placement.id);
                break;
            }
        }
    }
    mBoundRemote.reset();
    mBoundFile.clear();
    mBoundPlacementId = 0;
    mAppliedRevision = 0;
}

void PlacementBridge::retireAll() noexcept {
    mPending.reset();
    mBoundRemote.reset();
    mBoundFile.clear();
    mBoundPlacementId = 0;
    mAppliedRevision = 0;
}

} // namespace lholo::sync
