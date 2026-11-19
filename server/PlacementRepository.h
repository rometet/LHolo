#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "sync/common/Protocol.h"
#include "sync/common/SharedTypes.h"

struct sqlite3;

// SQLite-backed authoritative store for placements, files, permissions and the
// append-only catalog sequence (§5.3). All methods are intended to be called
// from the server's single sync worker/game thread; the class is not internally
// synchronised. A failed open or migration leaves the original database file
// intact and refuses new writes rather than silently reinitialising (§5.3).

namespace lholo::sync {

struct FileRecord {
    Sha256Hash sha256{};
    SchematicFormat format{SchematicFormat::Unknown};
    std::uint64_t sizeBytes{0};
    std::int64_t createdAtMs{0};
    bool validated{false};
};

class PlacementRepository {
public:
    PlacementRepository() = default;
    ~PlacementRepository();
    PlacementRepository(PlacementRepository const&)            = delete;
    PlacementRepository& operator=(PlacementRepository const&) = delete;

    // Opens (creating if absent) the database at `path`, applies PRAGMAs and the
    // schema/migrations. Returns false and sets `error` on any failure; the
    // existing file is never replaced.
    bool open(std::filesystem::path const& path, std::string& error);
    void close();
    [[nodiscard]] bool isOpen() const { return mDb != nullptr; }

    // --- files -------------------------------------------------------------
    // Returns true when the row already existed (deduplication).
    bool insertFile(FileRecord const& record, bool& existed, std::string& error);
    [[nodiscard]] std::optional<FileRecord> getFile(Sha256Hash const& hash);
    void setFileValidated(Sha256Hash const& hash, bool validated);

    // --- placements --------------------------------------------------------
    struct PlacementInput {
        Sha256Hash fileHash{};
        SchematicFormat format{SchematicFormat::Unknown};
        std::string displayName;
        std::int32_t dimension{0};
        std::int64_t originX{0};
        std::int64_t originY{0};
        std::int64_t originZ{0};
        std::int32_t rotation{0};
        std::int32_t mirror{0};
    };

    enum class Mutation { Ok, NotFound, Conflict, Forbidden };

    [[nodiscard]] std::optional<SharedPlacementDto> createPlacement(
        PlacementInput const& input,
        std::string const& ownerId,
        std::int64_t nowMs,
        std::string& error
    );

    [[nodiscard]] std::optional<SharedPlacementDto> getPlacement(Uuid const& placementId);

    // Applies a positional edit when `baseRevision` matches and the caller is the
    // owner (or an admin). Bumps revision, appends a catalog delta inside one
    // transaction.
    [[nodiscard]] bool updatePlacement(
        Uuid const& placementId,
        std::uint64_t baseRevision,
        PlacementInput const& input,
        std::string const& actorId,
        bool isAdmin,
        std::int64_t nowMs,
        SharedPlacementDto& out,
        Mutation& result,
        std::string& error
    );

    // Tombstone delete; owner or admin only. Advances the revision and appends a
    // delete event.
    [[nodiscard]] bool deletePlacement(
        Uuid const& placementId,
        std::uint64_t baseRevision,
        std::string const& actorId,
        bool isAdmin,
        std::int64_t nowMs,
        SharedPlacementDto& out,
        Mutation& result,
        std::string& error
    );

    // --- listing -----------------------------------------------------------
    struct Page {
        std::vector<SharedPlacementDto> items;
        std::optional<std::string> nextCursor;
        std::uint64_t snapshotSequence{0};
        bool hasMore{false};
    };

    [[nodiscard]] Page listPage(std::optional<std::string> const& cursor, std::uint32_t pageSize);

    [[nodiscard]] std::uint64_t currentSequence();
    [[nodiscard]] std::uint64_t activePlacementCount(std::string const& ownerId);
    [[nodiscard]] std::uint64_t totalPlacementCount();
    [[nodiscard]] std::uint64_t fileCount();

    // Files with no placement referencing them and created before the cutoff.
    struct GcCandidate {
        Sha256Hash sha256{};
        SchematicFormat format{SchematicFormat::Unknown};
        std::uint64_t sizeBytes{0};
    };
    [[nodiscard]] std::vector<GcCandidate> gcCandidates(std::int64_t createdBeforeMs);
    bool removeFile(Sha256Hash const& hash, std::string& error);

    // --- permissions -------------------------------------------------------
    [[nodiscard]] std::optional<Role> getRole(std::string const& playerId);
    [[nodiscard]] bool setRole(std::string const& playerId, Role role, std::int64_t nowMs, std::string& error);

    // --- client operation idempotency (§4.5) ------------------------------
    [[nodiscard]] std::optional<std::string> lookupOperation(std::string const& playerId, std::string const& clientOperationId);
    [[nodiscard]] bool recordOperation(
        std::string const& playerId,
        std::string const& clientOperationId,
        std::string const& action,
        std::string const& resultJson,
        std::int64_t nowMs,
        std::string& error
    );

private:
    [[nodiscard]] bool applySchema(std::string& error);
    [[nodiscard]] bool appendCatalogEvent(
        std::string const& action,
        Uuid const& placementId,
        std::string const& payloadJson,
        std::int64_t nowMs,
        std::string& error
    );
    [[nodiscard]] std::optional<SharedPlacementDto> readPlacementById(std::string const& id);

    sqlite3* mDb{nullptr};
};

} // namespace lholo::sync
