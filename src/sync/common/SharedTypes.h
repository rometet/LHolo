#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "Protocol.h"

// Shared value types for LHolo Sync. No Minecraft / LeviLamina headers here.

namespace lholo::sync {

// 128-bit identifier. Nil == all zero bytes.
struct Uuid {
    std::array<std::uint8_t, kUuidBytes> bytes{};

    [[nodiscard]] bool isNil() const noexcept;
    [[nodiscard]] std::string toString() const;   // lowercase 36-char canonical
    // Strict parser: lowercase/uppercase hex, canonical hyphen positions only.
    [[nodiscard]] static bool parse(std::string_view text, Uuid& out) noexcept;
    static Uuid random();                          // v4 from a CSPRNG

    [[nodiscard]] bool operator==(Uuid const&) const noexcept = default;
};

// Raw SHA-256 digest. `hex` is lowercase 64 chars.
struct Sha256Hash {
    std::array<std::uint8_t, kSha256Bytes> bytes{};

    [[nodiscard]] std::string toHex() const;       // lowercase 64-char
    [[nodiscard]] static bool parse(std::string_view hex, Sha256Hash& out) noexcept;
    [[nodiscard]] bool isZero() const noexcept;
    [[nodiscard]] bool operator==(Sha256Hash const&) const noexcept = default;
};

enum class SchematicFormat : std::uint8_t {
    Litematic = 0,
    Mcstructure = 1,
    Unknown = 0xff,
};

[[nodiscard]] constexpr char const* schematicFormatName(SchematicFormat format) noexcept {
    switch (format) {
        case SchematicFormat::Litematic: return "litematic";
        case SchematicFormat::Mcstructure: return "mcstructure";
        case SchematicFormat::Unknown: break;
    }
    return "unknown";
}

[[nodiscard]] constexpr bool parseSchematicFormat(std::string_view text, SchematicFormat& out) noexcept {
    if (text == "litematic") {
        out = SchematicFormat::Litematic;
        return true;
    }
    if (text == "mcstructure") {
        out = SchematicFormat::Mcstructure;
        return true;
    }
    return false;
}

// Server-authoritative shared placement record (§6.1). Display-only user
// preferences (layer, opacity, verifier, HUD, visibility) are deliberately
// absent: they remain client-local and are never synced.
struct SharedPlacementDto {
    Uuid placementId{};             // server-generated (never client-chosen on create)
    Sha256Hash fileHash{};
    SchematicFormat format{SchematicFormat::Unknown};
    std::string displayName;
    std::int32_t dimension{0};
    std::int64_t originX{0};
    std::int64_t originY{0};
    std::int64_t originZ{0};
    std::int32_t rotation{0};       // 0..3
    std::int32_t mirror{0};         // 0..2
    std::uint64_t revision{1};      // >= 1
    std::string ownerId;            // server-derived actor identity only
    std::int64_t updatedAtUnixMs{0};
    bool deleted{false};
};

// Server/config limits surfaced to the client in HELLO_ACK.
struct ServerLimits {
    std::uint64_t maxFileBytes{kDefaultMaxFileBytes};
    std::uint32_t maxActiveTransfers{2};
};

// Client session role (§7.1).
enum class Role : std::uint8_t { Viewer = 0, Member = 1, Admin = 2 };

[[nodiscard]] constexpr char const* roleName(Role role) noexcept {
    switch (role) {
        case Role::Viewer: return "viewer";
        case Role::Member: return "member";
        case Role::Admin: return "admin";
    }
    return "viewer";
}

[[nodiscard]] constexpr bool parseRole(std::string_view text, Role& out) noexcept {
    if (text == "viewer") { out = Role::Viewer; return true; }
    if (text == "member") { out = Role::Member; return true; }
    if (text == "admin")  { out = Role::Admin;  return true; }
    return false;
}

// Client-side shared-placement edit lifecycle (§4.5). The wire protocol carries
// only EDIT_BEGIN/RENEW/RELEASE and PLACEMENT_UPDATE; this state exists so the
// UI can gate the commit action on an actually granted server lease and report
// pending/failed/timeout feedback.
enum class EditPhase : std::uint8_t {
    Idle = 0,       // no edit in progress
    Pending,        // EDIT_BEGIN sent, waiting for the lease
    Granted,        // server lease held: the draft may be committed
    Committing,     // PLACEMENT_UPDATE sent, waiting for the result
    Applied,        // the last commit succeeded
    Failed,         // the last attempt failed; message carries the reason
};

[[nodiscard]] constexpr char const* editPhaseName(EditPhase phase) noexcept {
    switch (phase) {
        case EditPhase::Idle: return "idle";
        case EditPhase::Pending: return "pending";
        case EditPhase::Granted: return "granted";
        case EditPhase::Committing: return "committing";
        case EditPhase::Applied: return "applied";
        case EditPhase::Failed: return "failed";
    }
    return "idle";
}

// Error payload (§4.9) and a light status helper used by common code.
struct SyncError {
    ErrorCode code{ErrorCode::InvalidPayload};
    std::string message;            // English, log-oriented
    bool retryable{false};
    std::uint32_t retryAfterMs{0};
};

struct Status {
    bool ok{true};
    ErrorCode code{ErrorCode::InvalidPayload};
    std::string message;

    [[nodiscard]] static Status success() { return {}; }
    [[nodiscard]] static Status failure(ErrorCode code, std::string message) {
        Status status;
        status.ok = false;
        status.code = code;
        status.message = std::move(message);
        return status;
    }
};

} // namespace lholo::sync
