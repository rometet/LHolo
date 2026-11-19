#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Protocol.h"
#include "SharedTypes.h"

// Low-level validation primitives for control payloads (§4.3, §4.6). The typed
// JSON payload codecs live in ControlMessages.h; these primitives are shared by
// both the codecs and the tests and depend only on the standard library.

namespace lholo::sync::validation {

[[nodiscard]] bool isValidUtf8(std::string_view text) noexcept;

// Rejects C0/C1 controls (except none), and Unicode bidi override/isolate
// controls in display names (§4.6).
[[nodiscard]] bool isSafeDisplayText(std::string_view text) noexcept;

// 64-bit values travel as decimal strings (§4.3). Accepts an optional leading
// '-' for signed values; rejects leading '+', whitespace, empty and overflow.
[[nodiscard]] bool parseInt64String(std::string_view text, std::int64_t& out) noexcept;
[[nodiscard]] bool parseUint64String(std::string_view text, std::uint64_t& out) noexcept;

[[nodiscard]] std::string toString(std::int64_t value);
[[nodiscard]] std::string toString(std::uint64_t value);

// Display name limits (§4.6): NFC-agnostic but UTF-8 valid, <= 128 bytes, no
// control/NUL/path separators, no Windows reserved trailing dot/space.
[[nodiscard]] bool isValidDisplayName(std::string_view name) noexcept;

} // namespace lholo::sync::validation
