#include "PlacementRepository.h"

#include <sqlite3.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

#include "sync/common/ControlMessages.h"
#include "sync/common/Validation.h"

namespace lholo::sync {
namespace {

// RAII wrapper for a prepared statement.
class Statement {
public:
    Statement(sqlite3* db, char const* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &mStmt, nullptr) != SQLITE_OK) {
            mStmt = nullptr;
        }
    }
    ~Statement() {
        if (mStmt) {
            sqlite3_finalize(mStmt);
        }
    }
    Statement(Statement const&)            = delete;
    Statement& operator=(Statement const&) = delete;

    [[nodiscard]] bool ok() const { return mStmt != nullptr; }
    sqlite3_stmt* get() const { return mStmt; }

    bool bindText(int index, std::string const& value) {
        return sqlite3_bind_text(mStmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }
    bool bindInt64(int index, std::int64_t value) {
        return sqlite3_bind_int64(mStmt, index, value) == SQLITE_OK;
    }
    bool bindUInt64(int index, std::uint64_t value) {
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        return sqlite3_bind_int64(mStmt, index, static_cast<std::int64_t>(value)) == SQLITE_OK;
    }

    int step() { return sqlite3_step(mStmt); }
    [[nodiscard]] std::string columnText(int index) const {
        auto const* text = sqlite3_column_text(mStmt, index);
        return text ? std::string(reinterpret_cast<char const*>(text)) : std::string{};
    }
    [[nodiscard]] std::int64_t columnInt64(int index) const { return sqlite3_column_int64(mStmt, index); }

private:
    sqlite3_stmt* mStmt{nullptr};
};

class Transaction {
public:
    explicit Transaction(sqlite3* db) : mDb(db) {
        sqlite3_exec(mDb, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    }
    ~Transaction() {
        if (!mCommitted) {
            sqlite3_exec(mDb, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }
    void commit() {
        sqlite3_exec(mDb, "COMMIT", nullptr, nullptr, nullptr);
        mCommitted = true;
    }

private:
    sqlite3* mDb;
    bool mCommitted{false};
};

bool formatFromText(std::string const& text, SchematicFormat& format) {
    return parseSchematicFormat(text, format);
}

SharedPlacementDto readRowColumns(sqlite3_stmt* stmt, int base) {
    SharedPlacementDto dto;
    auto const text = [&](int i) {
        auto const* value = sqlite3_column_text(stmt, base + i);
        return value ? std::string(reinterpret_cast<char const*>(value)) : std::string{};
    };
    Uuid::parse(text(0), dto.placementId);
    Sha256Hash::parse(text(1), dto.fileHash);
    formatFromText(text(2), dto.format);
    dto.displayName = text(3);
    dto.dimension = static_cast<std::int32_t>(sqlite3_column_int64(stmt, base + 4));
    dto.originX = sqlite3_column_int64(stmt, base + 5);
    dto.originY = sqlite3_column_int64(stmt, base + 6);
    dto.originZ = sqlite3_column_int64(stmt, base + 7);
    dto.rotation = static_cast<std::int32_t>(sqlite3_column_int64(stmt, base + 8));
    dto.mirror = static_cast<std::int32_t>(sqlite3_column_int64(stmt, base + 9));
    dto.ownerId = text(10);
    dto.revision = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, base + 11));
    dto.updatedAtUnixMs = sqlite3_column_int64(stmt, base + 12);
    dto.deleted = sqlite3_column_int64(stmt, base + 13) != 0;
    return dto;
}

constexpr char kSelectColumns[] =
    "p.placement_id, p.file_sha256, f.format, p.name, p.dimension, p.origin_x, p.origin_y, p.origin_z, "
    "p.rotation, p.mirror, p.owner_id, p.revision, p.updated_at_ms, p.deleted_at_ms IS NOT NULL";

constexpr char kFromJoin[] = " FROM placements p JOIN files f ON f.sha256 = p.file_sha256";

std::string placementPayload(SharedPlacementDto const& dto) {
    // catalog_events and (via CATALOG_DELTA) clients carry the same snake_case
    // placement object defined in §4.6.
    auto const bytes = control::encodePlacement(dto);
    return std::string(reinterpret_cast<char const*>(bytes.data()), bytes.size());
}

} // namespace

PlacementRepository::~PlacementRepository() {
    close();
}

void PlacementRepository::close() {
    if (mDb) {
        sqlite3_close(mDb);
        mDb = nullptr;
    }
}

bool PlacementRepository::open(std::filesystem::path const& path, std::string& error) {
    close();
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);

    if (sqlite3_open_v2(path.string().c_str(), &mDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr)
        != SQLITE_OK) {
        error = std::string{"cannot open database: "} + (mDb ? sqlite3_errmsg(mDb) : "unknown");
        close();
        return false;
    }
    sqlite3_busy_timeout(mDb, 5000);

    char* err = nullptr;
    if (sqlite3_exec(mDb, "PRAGMA foreign_keys=ON;", nullptr, nullptr, &err) != SQLITE_OK) {
        error = std::string{"pragma foreign_keys failed: "} + (err ? err : "");
        sqlite3_free(err);
        close();
        return false;
    }
    if (sqlite3_exec(mDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err) != SQLITE_OK) {
        error = std::string{"pragma journal_mode failed: "} + (err ? err : "");
        sqlite3_free(err);
        close();
        return false;
    }

    if (!applySchema(error)) {
        close();
        return false;
    }
    return true;
}

bool PlacementRepository::applySchema(std::string& error) {
    static constexpr char const* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS files (
  sha256 TEXT PRIMARY KEY,
  format TEXT NOT NULL CHECK(format IN ('litematic','mcstructure')),
  size_bytes INTEGER NOT NULL CHECK(size_bytes > 0),
  created_at_ms INTEGER NOT NULL,
  validated INTEGER NOT NULL DEFAULT 0 CHECK(validated IN (0,1))
);
CREATE TABLE IF NOT EXISTS placements (
  placement_id TEXT PRIMARY KEY,
  file_sha256 TEXT NOT NULL REFERENCES files(sha256),
  name TEXT NOT NULL,
  dimension INTEGER NOT NULL,
  origin_x INTEGER NOT NULL, origin_y INTEGER NOT NULL, origin_z INTEGER NOT NULL,
  rotation INTEGER NOT NULL CHECK(rotation BETWEEN 0 AND 3),
  mirror INTEGER NOT NULL CHECK(mirror BETWEEN 0 AND 2),
  owner_id TEXT NOT NULL,
  revision INTEGER NOT NULL CHECK(revision >= 1),
  created_at_ms INTEGER NOT NULL,
  updated_at_ms INTEGER NOT NULL,
  deleted_at_ms INTEGER
);
CREATE TABLE IF NOT EXISTS permissions (
  player_id TEXT PRIMARY KEY,
  role TEXT NOT NULL CHECK(role IN ('viewer','member','admin')),
  updated_at_ms INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS operations (
  player_id TEXT NOT NULL,
  client_operation_id TEXT NOT NULL,
  action TEXT NOT NULL,
  result_json TEXT NOT NULL,
  created_at_ms INTEGER NOT NULL,
  PRIMARY KEY(player_id, client_operation_id)
);
CREATE TABLE IF NOT EXISTS catalog_events (
  sequence INTEGER PRIMARY KEY AUTOINCREMENT,
  action TEXT NOT NULL CHECK(action IN ('upsert','delete')),
  placement_id TEXT NOT NULL,
  payload_json TEXT NOT NULL,
  created_at_ms INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_placements_active_updated ON placements(deleted_at_ms, updated_at_ms);
CREATE INDEX IF NOT EXISTS idx_placements_owner ON placements(owner_id);
)SQL";

    char* err = nullptr;
    if (sqlite3_exec(mDb, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        error = std::string{"schema creation failed: "} + (err ? err : "");
        sqlite3_free(err);
        return false;
    }

    // schema_version is pinned to 1 for V1.
    Statement insert(mDb, "INSERT OR IGNORE INTO meta(key, value) VALUES('schema_version','1')");
    if (!insert.ok() || insert.step() != SQLITE_DONE) {
        error = "failed to record schema_version";
        return false;
    }
    return true;
}

bool PlacementRepository::insertFile(FileRecord const& record, bool& existed, std::string& error) {
    Statement select(mDb, "SELECT 1 FROM files WHERE sha256 = ?1");
    if (!select.ok() || !select.bindText(1, record.sha256.toHex())) {
        error = "prepare select file failed";
        return false;
    }
    existed = select.step() == SQLITE_ROW;
    if (existed) {
        return true;
    }

    Statement stmt(
        mDb,
        "INSERT INTO files(sha256, format, size_bytes, created_at_ms, validated) VALUES(?1, ?2, ?3, ?4, ?5)"
    );
    if (!stmt.ok() || !stmt.bindText(1, record.sha256.toHex())
        || !stmt.bindText(2, schematicFormatName(record.format)) || !stmt.bindUInt64(3, record.sizeBytes)
        || !stmt.bindInt64(4, record.createdAtMs) || !stmt.bindInt64(5, record.validated ? 1 : 0)) {
        error = "prepare insert file failed";
        return false;
    }
    if (stmt.step() != SQLITE_DONE) {
        error = std::string{"insert file failed: "} + sqlite3_errmsg(mDb);
        return false;
    }
    return true;
}

std::optional<FileRecord> PlacementRepository::getFile(Sha256Hash const& hash) {
    Statement stmt(mDb, "SELECT sha256, format, size_bytes, created_at_ms, validated FROM files WHERE sha256 = ?1");
    if (!stmt.ok() || !stmt.bindText(1, hash.toHex()) || stmt.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    FileRecord record;
    Sha256Hash::parse(stmt.columnText(0), record.sha256);
    formatFromText(stmt.columnText(1), record.format);
    record.sizeBytes = static_cast<std::uint64_t>(stmt.columnInt64(2));
    record.createdAtMs = stmt.columnInt64(3);
    record.validated = stmt.columnInt64(4) != 0;
    return record;
}

void PlacementRepository::setFileValidated(Sha256Hash const& hash, bool validated) {
    Statement stmt(mDb, "UPDATE files SET validated = ?2 WHERE sha256 = ?1");
    if (stmt.ok() && stmt.bindText(1, hash.toHex()) && stmt.bindInt64(2, validated ? 1 : 0)) {
        stmt.step();
    }
}

std::optional<SharedPlacementDto> PlacementRepository::readPlacementById(std::string const& id) {
    std::string sql = std::string{"SELECT "} + kSelectColumns + kFromJoin + " WHERE p.placement_id = ?1";
    Statement stmt(mDb, sql.c_str());
    if (!stmt.ok() || !stmt.bindText(1, id) || stmt.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return readRowColumns(stmt.get(), 0);
}

std::optional<SharedPlacementDto> PlacementRepository::createPlacement(
    PlacementInput const& input,
    std::string const& ownerId,
    std::int64_t nowMs,
    std::string& error
) {
    Uuid id = Uuid::random();
    std::string const idText = id.toString();

    Transaction transaction(mDb);
    Statement stmt(
        mDb,
        "INSERT INTO placements(placement_id, file_sha256, name, dimension, origin_x, origin_y, origin_z, "
        "rotation, mirror, owner_id, revision, created_at_ms, updated_at_ms) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, 1, ?11, ?11)"
    );
    if (!stmt.ok() || !stmt.bindText(1, idText) || !stmt.bindText(2, input.fileHash.toHex())
        || !stmt.bindText(3, input.displayName) || !stmt.bindInt64(4, input.dimension)
        || !stmt.bindInt64(5, input.originX) || !stmt.bindInt64(6, input.originY) || !stmt.bindInt64(7, input.originZ)
        || !stmt.bindInt64(8, input.rotation) || !stmt.bindInt64(9, input.mirror) || !stmt.bindText(10, ownerId)
        || !stmt.bindInt64(11, nowMs)) {
        error = "prepare create placement failed";
        return std::nullopt;
    }
    if (stmt.step() != SQLITE_DONE) {
        error = std::string{"create placement failed: "} + sqlite3_errmsg(mDb);
        return std::nullopt;
    }

    SharedPlacementDto dto;
    dto.placementId = id;
    dto.fileHash = input.fileHash;
    dto.format = input.format;
    dto.displayName = input.displayName;
    dto.dimension = input.dimension;
    dto.originX = input.originX;
    dto.originY = input.originY;
    dto.originZ = input.originZ;
    dto.rotation = input.rotation;
    dto.mirror = input.mirror;
    dto.revision = 1;
    dto.ownerId = ownerId;
    dto.updatedAtUnixMs = nowMs;

    if (!appendCatalogEvent("upsert", id, placementPayload(dto), nowMs, error)) {
        return std::nullopt;
    }
    transaction.commit();
    return dto;
}

std::optional<SharedPlacementDto> PlacementRepository::getPlacement(Uuid const& placementId) {
    return readPlacementById(placementId.toString());
}

bool PlacementRepository::updatePlacement(
    Uuid const& placementId,
    std::uint64_t baseRevision,
    PlacementInput const& input,
    std::string const& actorId,
    bool isAdmin,
    std::int64_t nowMs,
    SharedPlacementDto& out,
    Mutation& result,
    std::string& error
) {
    Transaction transaction(mDb);
    auto current = readPlacementById(placementId.toString());
    if (!current || current->deleted) {
        result = Mutation::NotFound;
        return true;
    }
    if (current->ownerId != actorId && !isAdmin) {
        result = Mutation::Forbidden;
        return true;
    }
    if (current->revision != baseRevision) {
        result = Mutation::Conflict;
        return true;
    }
    if (baseRevision + 1 < baseRevision) {
        error = "revision overflow";
        return false;
    }

    Statement stmt(
        mDb,
        "UPDATE placements SET name = ?2, dimension = ?3, origin_x = ?4, origin_y = ?5, origin_z = ?6, "
        "rotation = ?7, mirror = ?8, file_sha256 = ?9, revision = revision + 1, updated_at_ms = ?10 "
        "WHERE placement_id = ?1 AND revision = ?11 AND deleted_at_ms IS NULL"
    );
    if (!stmt.ok() || !stmt.bindText(1, placementId.toString()) || !stmt.bindText(2, input.displayName)
        || !stmt.bindInt64(3, input.dimension) || !stmt.bindInt64(4, input.originX) || !stmt.bindInt64(5, input.originY)
        || !stmt.bindInt64(6, input.originZ) || !stmt.bindInt64(7, input.rotation) || !stmt.bindInt64(8, input.mirror)
        || !stmt.bindText(9, input.fileHash.toHex()) || !stmt.bindInt64(10, nowMs) || !stmt.bindUInt64(11, baseRevision)) {
        error = "prepare update placement failed";
        return false;
    }
    if (stmt.step() != SQLITE_DONE) {
        error = std::string{"update placement failed: "} + sqlite3_errmsg(mDb);
        return false;
    }
    if (sqlite3_changes(mDb) == 0) {
        result = Mutation::Conflict; // lost the CAS race
        return true;
    }

    out = *current;
    out.displayName = input.displayName;
    out.dimension = input.dimension;
    out.originX = input.originX;
    out.originY = input.originY;
    out.originZ = input.originZ;
    out.rotation = input.rotation;
    out.mirror = input.mirror;
    out.fileHash = input.fileHash;
    out.format = input.format;
    out.revision = current->revision + 1;
    out.updatedAtUnixMs = nowMs;

    if (!appendCatalogEvent("upsert", placementId, placementPayload(out), nowMs, error)) {
        return false;
    }
    transaction.commit();
    result = Mutation::Ok;
    return true;
}

bool PlacementRepository::deletePlacement(
    Uuid const& placementId,
    std::uint64_t baseRevision,
    std::string const& actorId,
    bool isAdmin,
    std::int64_t nowMs,
    SharedPlacementDto& out,
    Mutation& result,
    std::string& error
) {
    Transaction transaction(mDb);
    auto current = readPlacementById(placementId.toString());
    if (!current || current->deleted) {
        result = Mutation::NotFound;
        return true;
    }
    if (current->ownerId != actorId && !isAdmin) {
        result = Mutation::Forbidden;
        return true;
    }
    if (current->revision != baseRevision) {
        result = Mutation::Conflict;
        return true;
    }

    Statement stmt(
        mDb,
        "UPDATE placements SET deleted_at_ms = ?2, revision = revision + 1, updated_at_ms = ?2 "
        "WHERE placement_id = ?1 AND revision = ?3 AND deleted_at_ms IS NULL"
    );
    if (!stmt.ok() || !stmt.bindText(1, placementId.toString()) || !stmt.bindInt64(2, nowMs)
        || !stmt.bindUInt64(3, baseRevision)) {
        error = "prepare delete placement failed";
        return false;
    }
    if (stmt.step() != SQLITE_DONE) {
        error = std::string{"delete placement failed: "} + sqlite3_errmsg(mDb);
        return false;
    }
    if (sqlite3_changes(mDb) == 0) {
        result = Mutation::Conflict;
        return true;
    }

    out = *current;
    out.deleted = true;
    out.revision = current->revision + 1;
    out.updatedAtUnixMs = nowMs;

    if (!appendCatalogEvent("delete", placementId, placementPayload(out), nowMs, error)) {
        return false;
    }
    transaction.commit();
    result = Mutation::Ok;
    return true;
}

PlacementRepository::Page PlacementRepository::listPage(std::optional<std::string> const& cursor, std::uint32_t pageSize) {
    Page page;
    if (pageSize == 0 || pageSize > kMaxCatalogPageSize) {
        pageSize = kMaxCatalogPageSize;
    }
    page.snapshotSequence = currentSequence();

    std::string sql = std::string{"SELECT "} + kSelectColumns + kFromJoin
        + " WHERE p.deleted_at_ms IS NULL";
    std::int64_t cursorUpdated = 0;
    std::string cursorId;
    bool hasCursor = false;
    if (cursor && !cursor->empty()) {
        auto const separator = cursor->find(':');
        if (separator != std::string::npos) {
            std::string const updatedText = cursor->substr(0, separator);
            cursorId = cursor->substr(separator + 1);
            if (validation::parseInt64String(updatedText, cursorUpdated)) {
                hasCursor = true;
            }
        }
    }
    int limitPlaceholder = 1;
    if (hasCursor) {
        sql += " AND (p.updated_at_ms < ?1 OR (p.updated_at_ms = ?1 AND p.placement_id < ?2))";
        limitPlaceholder = 3;
    }
    sql += " ORDER BY p.updated_at_ms DESC, p.placement_id DESC LIMIT ?" + std::to_string(limitPlaceholder);

    Statement stmt(mDb, sql.c_str());
    if (!stmt.ok()) {
        return page;
    }
    int index = 1;
    if (hasCursor) {
        stmt.bindInt64(index++, cursorUpdated);
        stmt.bindText(index++, cursorId);
    }
    stmt.bindInt64(limitPlaceholder, static_cast<std::int64_t>(pageSize) + 1);

    while (stmt.step() == SQLITE_ROW) {
        page.items.push_back(readRowColumns(stmt.get(), 0));
    }
    if (page.items.size() > pageSize) {
        page.hasMore = true;
        page.items.resize(pageSize);
        auto const& last = page.items.back();
        page.nextCursor = std::to_string(last.updatedAtUnixMs) + ":" + last.placementId.toString();
    } else {
        page.nextCursor.reset();
    }
    return page;
}

std::uint64_t PlacementRepository::currentSequence() {
    Statement stmt(mDb, "SELECT COALESCE(MAX(sequence), 0) FROM catalog_events");
    if (!stmt.ok() || stmt.step() != SQLITE_ROW) {
        return 0;
    }
    return static_cast<std::uint64_t>(stmt.columnInt64(0));
}

std::uint64_t PlacementRepository::activePlacementCount(std::string const& ownerId) {
    Statement stmt(mDb, "SELECT COUNT(*) FROM placements WHERE deleted_at_ms IS NULL AND owner_id = ?1");
    if (!stmt.ok() || !stmt.bindText(1, ownerId) || stmt.step() != SQLITE_ROW) {
        return 0;
    }
    return static_cast<std::uint64_t>(stmt.columnInt64(0));
}

std::uint64_t PlacementRepository::totalPlacementCount() {
    Statement stmt(mDb, "SELECT COUNT(*) FROM placements WHERE deleted_at_ms IS NULL");
    if (!stmt.ok() || stmt.step() != SQLITE_ROW) {
        return 0;
    }
    return static_cast<std::uint64_t>(stmt.columnInt64(0));
}

std::uint64_t PlacementRepository::fileCount() {
    Statement stmt(mDb, "SELECT COUNT(*) FROM files");
    if (!stmt.ok() || stmt.step() != SQLITE_ROW) {
        return 0;
    }
    return static_cast<std::uint64_t>(stmt.columnInt64(0));
}

std::vector<PlacementRepository::GcCandidate> PlacementRepository::gcCandidates(std::int64_t createdBeforeMs) {
    std::vector<GcCandidate> result;
    Statement stmt(
        mDb,
        "SELECT f.sha256, f.format, f.size_bytes FROM files f "
        "WHERE f.created_at_ms < ?1 AND NOT EXISTS "
        "(SELECT 1 FROM placements p WHERE p.file_sha256 = f.sha256)"
    );
    if (!stmt.ok() || !stmt.bindInt64(1, createdBeforeMs)) {
        return result;
    }
    while (stmt.step() == SQLITE_ROW) {
        GcCandidate candidate;
        Sha256Hash::parse(stmt.columnText(0), candidate.sha256);
        formatFromText(stmt.columnText(1), candidate.format);
        candidate.sizeBytes = static_cast<std::uint64_t>(stmt.columnInt64(2));
        result.push_back(candidate);
    }
    return result;
}

bool PlacementRepository::removeFile(Sha256Hash const& hash, std::string& error) {
    Statement stmt(mDb, "DELETE FROM files WHERE sha256 = ?1");
    if (!stmt.ok() || !stmt.bindText(1, hash.toHex())) {
        error = "prepare remove file failed";
        return false;
    }
    if (stmt.step() != SQLITE_DONE) {
        error = std::string{"remove file failed: "} + sqlite3_errmsg(mDb);
        return false;
    }
    return true;
}

std::optional<Role> PlacementRepository::getRole(std::string const& playerId) {
    Statement stmt(mDb, "SELECT role FROM permissions WHERE player_id = ?1");
    if (!stmt.ok() || !stmt.bindText(1, playerId) || stmt.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    Role role;
    return parseRole(stmt.columnText(0), role) ? std::optional<Role>{role} : std::nullopt;
}

bool PlacementRepository::setRole(std::string const& playerId, Role role, std::int64_t nowMs, std::string& error) {
    Statement stmt(
        mDb,
        "INSERT INTO permissions(player_id, role, updated_at_ms) VALUES(?1, ?2, ?3) "
        "ON CONFLICT(player_id) DO UPDATE SET role = ?2, updated_at_ms = ?3"
    );
    if (!stmt.ok() || !stmt.bindText(1, playerId) || !stmt.bindText(2, roleName(role)) || !stmt.bindInt64(3, nowMs)) {
        error = "prepare set role failed";
        return false;
    }
    if (stmt.step() != SQLITE_DONE) {
        error = std::string{"set role failed: "} + sqlite3_errmsg(mDb);
        return false;
    }
    return true;
}

std::optional<std::string> PlacementRepository::lookupOperation(
    std::string const& playerId,
    std::string const& clientOperationId
) {
    Statement stmt(mDb, "SELECT result_json FROM operations WHERE player_id = ?1 AND client_operation_id = ?2");
    if (!stmt.ok() || !stmt.bindText(1, playerId) || !stmt.bindText(2, clientOperationId) || stmt.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return stmt.columnText(0);
}

bool PlacementRepository::recordOperation(
    std::string const& playerId,
    std::string const& clientOperationId,
    std::string const& action,
    std::string const& resultJson,
    std::int64_t nowMs,
    std::string& error
) {
    Statement stmt(
        mDb,
        "INSERT OR IGNORE INTO operations(player_id, client_operation_id, action, result_json, created_at_ms) "
        "VALUES(?1, ?2, ?3, ?4, ?5)"
    );
    if (!stmt.ok() || !stmt.bindText(1, playerId) || !stmt.bindText(2, clientOperationId)
        || !stmt.bindText(3, action) || !stmt.bindText(4, resultJson) || !stmt.bindInt64(5, nowMs)) {
        error = "prepare record operation failed";
        return false;
    }
    if (stmt.step() != SQLITE_DONE) {
        error = std::string{"record operation failed: "} + sqlite3_errmsg(mDb);
        return false;
    }
    return true;
}

bool PlacementRepository::appendCatalogEvent(
    std::string const& action,
    Uuid const& placementId,
    std::string const& payloadJson,
    std::int64_t nowMs,
    std::string& error
) {
    Statement stmt(
        mDb,
        "INSERT INTO catalog_events(action, placement_id, payload_json, created_at_ms) VALUES(?1, ?2, ?3, ?4)"
    );
    if (!stmt.ok() || !stmt.bindText(1, action) || !stmt.bindText(2, placementId.toString())
        || !stmt.bindText(3, payloadJson) || !stmt.bindInt64(4, nowMs)) {
        error = "prepare catalog event failed";
        return false;
    }
    if (stmt.step() != SQLITE_DONE) {
        error = std::string{"catalog event failed: "} + sqlite3_errmsg(mDb);
        return false;
    }
    return true;
}

} // namespace lholo::sync
