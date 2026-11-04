#pragma once

#include <cstddef>
#include <cstdint>

// LHolo Sync Protocol 1.0 — wire constants.
//
// This header is intentionally free of Minecraft / LeviLamina headers so it can
// be compiled into both the client mod and the dedicated server plugin. See the
// implementation specification §4.2 (packet registration), §4.3 (frame format),
// §4.5 (opcodes), §4.7 (file transfer) and §4.9 (error codes).

namespace lholo::sync {

// §4.2 — the single low-level transport packet both sides register.
inline constexpr char const kCustomPacketId[] = "lholo:sync_frame";
inline constexpr std::uint32_t kPacketVersion = 1;

// §4.3 — application frame.
inline constexpr char kFrameMagic[4] = {'L', 'H', 'S', '1'};

inline constexpr std::uint16_t kProtocolMajor = 1;
inline constexpr std::uint16_t kProtocolMinor = 0;

inline constexpr std::size_t kFrameHeaderSize = 40;             // bytes[0..39]
inline constexpr std::size_t kMaxFrameBytes = 65536;            // total frame
inline constexpr std::size_t kMaxPayloadBytes = 65496;          // frame - header
inline constexpr std::size_t kMaxControlJsonBytes = 32768;      // control JSON
inline constexpr std::size_t kMaxFileChunkBytes = 49152;        // FILE_CHUNK data
inline constexpr std::size_t kSessionIdBytes = 16;
inline constexpr std::size_t kSha256Bytes = 32;
inline constexpr std::size_t kSha256HexLength = 64;
inline constexpr std::size_t kUuidBytes = 16;
inline constexpr std::size_t kUuidStringLength = 36;

// Fixed FILE_CHUNK payload prefix (§4.7): transfer UUID + offset + length.
inline constexpr std::size_t kFileChunkPrefixBytes = 28;

// §4.5 opcodes.
enum class Opcode : std::uint16_t {
    Hello = 1,          // C->S
    HelloAck = 2,       // S->C
    ListRequest = 3,    // C->S
    ListPage = 4,       // S->C
    CatalogDelta = 5,   // S->C
    FileOffer = 6,      // C->S
    FileGet = 7,        // C->S
    FileAccept = 8,     // S->C or C->S
    FileChunk = 9,      // both
    FileAck = 10,       // both
    FileComplete = 11,  // both
    PlacementCreate = 12, // C->S
    PlacementUpdate = 13, // C->S
    PlacementDelete = 14, // C->S
    EditBegin = 15,     // C->S
    EditGranted = 16,   // S->C
    EditRelease = 17,   // C->S
    EditRenew = 18,     // C->S
    Result = 19,        // S->C
    Error = 20,         // S->C or C->S
};

[[nodiscard]] constexpr bool isKnownOpcode(std::uint16_t value) noexcept {
    return value >= static_cast<std::uint16_t>(Opcode::Hello)
        && value <= static_cast<std::uint16_t>(Opcode::Error);
}

// §4.9 error codes (fixed numeric values).
enum class ErrorCode : std::uint16_t {
    UnsupportedVersion = 1001,
    Unauthorized = 1002,
    Forbidden = 1003,
    NotFound = 1004,
    RevisionConflict = 1005,
    Locked = 1006,
    InvalidPayload = 1007,
    FileTooLarge = 1008,
    HashMismatch = 1009,
    RateLimited = 1010,
    ServerBusy = 1011,
    TransferTimeout = 1012,
    UnsupportedFormat = 1013,
    SubregionUnsupported = 1014,
    StorageError = 1015,
    WorldChanged = 1016,
    CapacityExceeded = 1017,
};

[[nodiscard]] constexpr char const* errorCodeName(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::UnsupportedVersion: return "UNSUPPORTED_VERSION";
        case ErrorCode::Unauthorized: return "UNAUTHORIZED";
        case ErrorCode::Forbidden: return "FORBIDDEN";
        case ErrorCode::NotFound: return "NOT_FOUND";
        case ErrorCode::RevisionConflict: return "REVISION_CONFLICT";
        case ErrorCode::Locked: return "LOCKED";
        case ErrorCode::InvalidPayload: return "INVALID_PAYLOAD";
        case ErrorCode::FileTooLarge: return "FILE_TOO_LARGE";
        case ErrorCode::HashMismatch: return "HASH_MISMATCH";
        case ErrorCode::RateLimited: return "RATE_LIMITED";
        case ErrorCode::ServerBusy: return "SERVER_BUSY";
        case ErrorCode::TransferTimeout: return "TRANSFER_TIMEOUT";
        case ErrorCode::UnsupportedFormat: return "UNSUPPORTED_FORMAT";
        case ErrorCode::SubregionUnsupported: return "SUBREGION_UNSUPPORTED";
        case ErrorCode::StorageError: return "STORAGE_ERROR";
        case ErrorCode::WorldChanged: return "WORLD_CHANGED";
        case ErrorCode::CapacityExceeded: return "CAPACITY_EXCEEDED";
    }
    return "UNKNOWN";
}

// §4.1/§4.8 — session negotiation timings and transfer window defaults.
inline constexpr int kPeerCapabilityTimeoutMs = 5000;   // give up after 5 s
inline constexpr int kRequestTimeoutMs = 10000;         // request timeout 10 s
inline constexpr int kRequestRetries = 2;               // 2 retries -> explicit error
inline constexpr std::uint32_t kDefaultChunkBytes = static_cast<std::uint32_t>(kMaxFileChunkBytes);
inline constexpr std::uint32_t kDefaultWindow = 4;

// §4.6 — dimension enumeration accepted by V1.
inline constexpr int kDimensionOverworld = 0;
inline constexpr int kDimensionNether = 1;
inline constexpr int kDimensionEnd = 2;

// §4.6 — placement origin bounds (matches existing LHolo PlacementSession).
inline constexpr std::int64_t kMaxOriginXZ = 30000000;
inline constexpr std::int64_t kMaxOriginY = 1000000;

// §4.6 — display name limit.
inline constexpr std::size_t kMaxDisplayNameBytes = 128;

// §4.7 — V1 default content limits (server config may lower, never raise the
// protocol's absolute ceiling).
inline constexpr std::uint64_t kDefaultMaxFileBytes = 128ull * 1024 * 1024;      // 128 MiB
inline constexpr std::uint64_t kAbsoluteMaxFileBytes = 256ull * 1024 * 1024;     // 256 MiB

// §7.3 — strict receive-side structural limits.
inline constexpr std::uint64_t kMaxReceivedInflationBytes = 512ull * 1024 * 1024; // 512 MiB
inline constexpr std::uint64_t kMaxTotalBlockCells = 100000000ull;                 // 100M cells
inline constexpr std::size_t kMaxNbtNesting = 64;
inline constexpr std::size_t kMaxJsonDepth = 16;
inline constexpr std::size_t kMaxJsonArrayItems = 50;
inline constexpr std::size_t kMaxJsonStringBytes = 1024;
inline constexpr std::size_t kMaxCatalogPageSize = 50;

} // namespace lholo::sync
