#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "sync/common/SharedTypes.h"

// P1-C bridge: binds a server-authoritative shared placement to the existing
// LHolo single-active-projection machinery. It never creates a second renderer
// path; it imports the verified cache file into the LHolo library and drives
// the public SchematicRuntime API (`place` + `edit`) exactly like a local load.
//
// Invariants (see docs/SCHEMATIC_FEATURE_AUDIT.md and the Sync spec §6.3):
//   * Must run on the game/control thread (Present/tick), never the network
//     handler thread.
//   * A placement in a different dimension is deferred, never misplaced.
//   * Only the server's origin/rotation/mirror/dimension are applied; local
//     layer/opacity/HUD/verifier settings stay client-owned and untouched.
//   * Removing a binding never deletes the local library file or a user's
//     ordinary placement.

namespace lholo::sync {

class PlacementBridge {
public:
    struct Result {
        bool placed{false};
        bool deferredDimension{false};
        std::string status;   // English, log/UI-oriented
    };

    // Prepares and (if the dimension matches) starts projecting the shared
    // placement. `verifiedCachePath` must be the SHA-256-verified cache object.
    [[nodiscard]] Result activate(
        SharedPlacementDto const& placement,
        std::filesystem::path const& verifiedCachePath
    );

    // Applies a remote transform-only update if this placement is bound and the
    // underlying file is unchanged (no re-download, no re-import).
    [[nodiscard]] bool applyRemoteUpdate(SharedPlacementDto const& placement);

    // Drives deferred dimension waits and the post-load transform application.
    void tick();

    // Removes the binding; the library file and other local placements remain.
    void retire(Uuid const& remotePlacementId);
    void retireAll() noexcept;

    [[nodiscard]] std::optional<Uuid> boundPlacement() const { return mBoundRemote; }
    [[nodiscard]] std::uint64_t appliedRevision() const { return mAppliedRevision; }

private:
    struct Pending {
        SharedPlacementDto placement;
        std::filesystem::path cachePath;
        std::string libraryRelative;
    };

    [[nodiscard]] bool importToLibrary(
        SharedPlacementDto const& placement,
        std::filesystem::path const& cachePath,
        std::string& libraryRoot,
        std::string& libraryRelative,
        std::string& error
    );
    [[nodiscard]] bool applyTransformToSelected();

    std::optional<Pending> mPending;
    std::optional<Uuid> mBoundRemote;
    std::string mBoundFile;      // library-relative file for the bound placement
    std::uint64_t mAppliedRevision{0};
};

} // namespace lholo::sync
