#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "Protocol.h"
#include "SharedTypes.h"

// LHS1 application frame codec (§4.3). Little-endian on the wire. Decoding is
// strict: unknown opcodes, non-zero flags, trailing bytes, bad magic and
// out-of-range lengths are all rejected without allocating unbounded memory.

namespace lholo::sync {

struct Frame {
    std::uint16_t opcode{0};                                 // validates to Opcode
    std::uint16_t flags{0};                                  // V1 requires 0
    std::uint64_t requestId{0};
    std::array<std::uint8_t, kSessionIdBytes> sessionId{};   // nil before HELLO_ACK
    std::vector<std::uint8_t> payload;

    [[nodiscard]] Opcode op() const noexcept { return static_cast<Opcode>(opcode); }
};

// Distinct outcome for version negotiation so the caller can reply
// ERROR(UNSUPPORTED_VERSION) rather than INVALID_PAYLOAD.
enum class DecodeResult {
    Ok,
    IncompatibleVersion,
    Invalid,
};

// Serializes `frame` into `out` (cleared first). Returns false if the payload
// exceeds the protocol limit.
[[nodiscard]] bool encodeFrame(Frame const& frame, std::vector<std::uint8_t>& out);

// Strictly decodes one frame from `bytes`. On success `out` is fully populated
// and `bytes` must be exactly one frame. `error` receives an English reason.
[[nodiscard]] DecodeResult decodeFrame(
    std::span<std::uint8_t const> bytes,
    Frame& out,
    std::string& error
);

// --- FILE_CHUNK binary payload (§4.7) -------------------------------------
struct FileChunkPayload {
    std::array<std::uint8_t, kUuidBytes> transferId{};
    std::uint64_t offset{0};
    std::uint32_t length{0};       // 1..kMaxFileChunkBytes
    std::span<std::uint8_t const> data;
};

[[nodiscard]] bool encodeFileChunk(
    std::array<std::uint8_t, kUuidBytes> const& transferId,
    std::uint64_t offset,
    std::span<std::uint8_t const> data,
    std::vector<std::uint8_t>& out
);

// Decodes a FILE_CHUNK payload; `chunk.data` points into `bytes`.
[[nodiscard]] bool decodeFileChunk(
    std::span<std::uint8_t const> bytes,
    FileChunkPayload& chunk,
    std::string& error
);

// --- little-endian helpers (shared by adapters and tests) ------------------
namespace le {

inline void writeU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

inline void writeU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

inline void writeU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

[[nodiscard]] inline std::uint16_t readU16(std::uint8_t const* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

[[nodiscard]] inline std::uint32_t readU32(std::uint8_t const* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[nodiscard]] inline std::uint64_t readU64(std::uint8_t const* p) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    }
    return value;
}

} // namespace le

} // namespace lholo::sync
