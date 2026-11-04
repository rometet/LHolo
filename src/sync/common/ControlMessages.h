#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Protocol.h"
#include "SharedTypes.h"

// Typed codecs for the control-payload JSON objects (§4.6, §4.7). Encoding
// produces compact UTF-8 JSON with 64-bit values as decimal strings; decoding
// is strict (duplicate keys, depth, ranges, required fields).
//
// These functions only need nlohmann/json internally; the header exposes plain
// standard-library types so callers do not depend on the JSON library.

namespace lholo::sync::control {

// --- HELLO / HELLO_ACK (§4.4) ---------------------------------------------
struct Hello {
    std::string clientVersion;
    std::uint16_t protocolMajor{kProtocolMajor};
    std::uint16_t protocolMinor{kProtocolMinor};
    std::vector<std::string> features;   // "files","placements","edit_lock"
};

struct HelloAck {
    std::string serverVersion;
    Uuid serverId;
    Uuid worldId;
    Uuid sessionId;
    std::vector<std::string> features;
    Role role{Role::Viewer};
    std::uint64_t maxFileBytes{kDefaultMaxFileBytes};
    std::uint32_t maxActiveTransfers{2};
    std::uint64_t catalogSequence{0};
};

// --- LIST (§4.6) -----------------------------------------------------------
struct ListRequest {
    std::optional<std::string> cursor;   // opaque, null on first page
    std::uint32_t pageSize{kMaxCatalogPageSize};
    std::uint64_t knownSequence{0};
};

struct ListPage {
    std::vector<SharedPlacementDto> items;
    std::optional<std::string> nextCursor;
    std::uint64_t snapshotSequence{0};
    bool hasMore{false};
};

struct CatalogDelta {
    std::uint64_t sequence{0};
    std::string action;                  // "upsert" | "delete"
    SharedPlacementDto placement;        // tombstone carries id/revision/deleted
};

// --- FILE transfer (§4.7) --------------------------------------------------
struct FileOffer {
    Uuid clientOperationId;
    Sha256Hash sha256;
    SchematicFormat format{SchematicFormat::Unknown};
    std::uint64_t sizeBytes{0};
    std::string displayFilename;
};

struct FileGet {
    Uuid placementId;
    Sha256Hash sha256;
    std::uint64_t resumeOffset{0};
};

struct FileAccept {
    std::string status;                  // "offer" | "ready" | "skip"
    Uuid transferId;
    bool needBytes{true};
    std::uint64_t sizeBytes{0};
    std::uint64_t resumeOffset{0};
    std::uint32_t chunkBytes{kDefaultChunkBytes};
    std::uint32_t window{kDefaultWindow};
};

struct FileAck {
    Uuid transferId;
    std::uint64_t nextOffset{0};
    std::uint32_t receivedBitmap{0};
};

struct FileComplete {
    Uuid transferId;
    Sha256Hash sha256;
    std::string status;                  // "request" | "ok" | "failed"
};

// --- placements / edits (§4.6) --------------------------------------------
struct PlacementCreate {
    Uuid clientOperationId;
    std::string displayName;
    Sha256Hash fileSha256;
    SchematicFormat fileFormat{SchematicFormat::Unknown};
    std::int32_t dimension{0};
    std::int64_t originX{0};
    std::int64_t originY{0};
    std::int64_t originZ{0};
    std::int32_t rotation{0};
    std::int32_t mirror{0};
};

struct PlacementUpdate {
    Uuid clientOperationId;
    Uuid placementId;
    std::uint64_t baseRevision{0};
    Uuid lockId;
    std::string displayName;
    std::int32_t dimension{0};
    std::int64_t originX{0};
    std::int64_t originY{0};
    std::int64_t originZ{0};
    std::int32_t rotation{0};
    std::int32_t mirror{0};
};

struct PlacementDelete {
    Uuid clientOperationId;
    Uuid placementId;
    std::uint64_t baseRevision{0};
};

struct EditBegin {
    Uuid placementId;
    std::uint64_t baseRevision{0};
};

struct EditGranted {
    Uuid placementId;
    std::uint64_t baseRevision{0};
    Uuid lockId;
    std::int64_t expiresAtUnixMs{0};
};

struct EditLease {
    Uuid placementId;
    Uuid lockId;
};

// RESULT carries request-type-specific success data; the union here keeps the
// codec small while covering CREATE/UPDATE editions and transfer grants.
struct Result {
    std::optional<Uuid> placementId;
    std::optional<std::uint64_t> revision;
    std::optional<std::uint64_t> catalogSequence;
    std::optional<Uuid> transferId;
    std::string status;                  // "ok" plus optional context
};

struct ErrorMessage {
    ErrorCode code{ErrorCode::InvalidPayload};
    std::string message;
    bool retryable{false};
    std::uint32_t retryAfterMs{0};
};

// --- encode -------------------------------------------------------------
[[nodiscard]] std::vector<std::uint8_t> encode(Hello const&);
[[nodiscard]] std::vector<std::uint8_t> encode(HelloAck const&);
[[nodiscard]] std::vector<std::uint8_t> encode(ListRequest const&);
[[nodiscard]] std::vector<std::uint8_t> encode(ListPage const&);
[[nodiscard]] std::vector<std::uint8_t> encode(CatalogDelta const&);
[[nodiscard]] std::vector<std::uint8_t> encode(FileOffer const&);
[[nodiscard]] std::vector<std::uint8_t> encode(FileGet const&);
[[nodiscard]] std::vector<std::uint8_t> encode(FileAccept const&);
[[nodiscard]] std::vector<std::uint8_t> encode(FileAck const&);
[[nodiscard]] std::vector<std::uint8_t> encode(FileComplete const&);
[[nodiscard]] std::vector<std::uint8_t> encode(PlacementCreate const&);
[[nodiscard]] std::vector<std::uint8_t> encode(PlacementUpdate const&);
[[nodiscard]] std::vector<std::uint8_t> encode(PlacementDelete const&);
[[nodiscard]] std::vector<std::uint8_t> encode(EditBegin const&);
[[nodiscard]] std::vector<std::uint8_t> encode(EditGranted const&);
[[nodiscard]] std::vector<std::uint8_t> encode(EditLease const&);
[[nodiscard]] std::vector<std::uint8_t> encode(Result const&);
[[nodiscard]] std::vector<std::uint8_t> encode(ErrorMessage const&);

// Placement object shared by LIST_PAGE / CATALOG_DELTA / RESULT payloads.
[[nodiscard]] std::vector<std::uint8_t> encodePlacement(SharedPlacementDto const&);

// --- decode (strict) -----------------------------------------------------
[[nodiscard]] Status decode(std::span<std::uint8_t const>, Hello&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, HelloAck&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, ListRequest&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, ListPage&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, CatalogDelta&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, FileOffer&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, FileGet&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, FileAccept&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, FileAck&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, FileComplete&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, PlacementCreate&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, PlacementUpdate&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, PlacementDelete&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, EditBegin&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, EditGranted&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, EditLease&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, Result&);
[[nodiscard]] Status decode(std::span<std::uint8_t const>, ErrorMessage&);

[[nodiscard]] Status decodePlacement(std::span<std::uint8_t const>, SharedPlacementDto&);

} // namespace lholo::sync::control
