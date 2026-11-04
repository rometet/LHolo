#include "sync/client/PlacementBridge.h"

#include <filesystem>

#include "structure/PlacementSession.h"
#include "structure/SchematicRuntime.h"

#include "sync/common/PathSupport.h"
#include "sync/common/Protocol.h"

namespace lholo::sync {
namespace {

std::string extensionFor(SchematicFormat format) {
    switch (format) {
        case SchematicFormat::Litematic: return ".litematic";
        case SchematicFormat::Mcstructure: return ".mcstructure";
        case SchematicFormat::Unknown: break;
    }
    return ".bin";
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
    libraryRelative = placement.fileHash.toHex() + extensionFor(placement.format);

    std::filesystem::path const destination =
        std::filesystem::path(libraryRoot) / libraryRelative;
    std::error_code code;
    if (!std::filesystem::is_regular_file(paths::ioPath(destination), code)) {
        std::filesystem::create_directories(paths::ioPath(destination.parent_path()), code);
        std::filesystem::copy_file(
            paths::ioPath(cachePath), paths::ioPath(destination),
            std::filesystem::copy_options::overwrite_existing, code
        );
        if (code) {
            error = "cannot import shared file into the library: " + code.message();
            return false;
        }
    }
    return true;
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

    mPending = Pending{placement, verifiedCachePath, libraryRelative};
    mBoundRemote = placement.placementId;

    if (snap.dimension != placement.dimension) {
        result.deferredDimension = true;
        result.status = "waiting for dimension";
        return result;
    }

    if (!structure::schematic::place(libraryRelative)) {
        mPending.reset();
        result.status = "could not start projection";
        return result;
    }
    result.placed = true;
    result.status = "placed";
    return result;
}

bool PlacementBridge::applyTransformToSelected() {
    if (!mPending) {
        return false;
    }
    auto const snap = structure::schematic::snapshot();
    if (!snap.worldAvailable || !snap.session.writable) {
        return false;
    }
    auto const& placements = snap.session.document.placements;
    for (auto const& placement : placements) {
        if (placement.file != mPending->libraryRelative) {
            continue;
        }
        if (snap.dimension != mPending->placement.dimension) {
            return false; // still waiting for the right dimension
        }
        structure::SavedPlacement updated = placement;
        applyServerTransform(updated, mPending->placement);
        if (updated != placement) {
            if (!structure::schematic::edit(updated, snap.session.revision)) {
                return false;
            }
        }
        mBoundFile = mPending->libraryRelative;
        mAppliedRevision = mPending->placement.revision;
        mPending.reset();
        return true;
    }
    return false;
}

void PlacementBridge::tick() {
    if (!mPending) {
        return;
    }
    auto const snap = structure::schematic::snapshot();
    if (!snap.worldAvailable) {
        return;
    }
    // Deferred activation: wait for the target dimension, then start projecting.
    if (snap.dimension != mPending->placement.dimension) {
        return;
    }
    bool present = false;
    for (auto const& placement : snap.session.document.placements) {
        if (placement.file == mPending->libraryRelative) {
            present = true;
            break;
        }
    }
    if (!present && !structure::schematic::place(mPending->libraryRelative)) {
        return;
    }
    applyTransformToSelected();
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
        if (current.file != mBoundFile) {
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
    if (!mBoundFile.empty()) {
        auto const snap = structure::schematic::snapshot();
        for (auto const& placement : snap.session.document.placements) {
            if (placement.file == mBoundFile) {
                structure::schematic::erase(placement.id);
                break;
            }
        }
    }
    mBoundRemote.reset();
    mBoundFile.clear();
    mAppliedRevision = 0;
}

void PlacementBridge::retireAll() noexcept {
    mPending.reset();
    mBoundRemote.reset();
    mBoundFile.clear();
    mAppliedRevision = 0;
}

} // namespace lholo::sync
