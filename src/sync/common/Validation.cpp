#include "Validation.h"

#include <limits>

namespace lholo::sync::validation {
namespace {

[[nodiscard]] bool isContinuation(std::uint8_t byte) noexcept {
    return (byte & 0xC0) == 0x80;
}

// Unicode bidi override / isolate / embedding controls are unsafe in display
// text (U+202A..U+202E, U+2066..U+2069). Encoded here as UTF-8 byte sequences.
[[nodiscard]] bool isBidiControlAt(std::string_view text, std::size_t index) noexcept {
    static constexpr char const* kSequences[] = {
        "\xE2\x80\xAA", // U+202A LRE
        "\xE2\x80\xAB", // U+202B RLE
        "\xE2\x80\xAC", // U+202C PDF
        "\xE2\x80\xAD", // U+202D LRO
        "\xE2\x80\xAE", // U+202E RLO
        "\xE2\x81\xA6", // U+2066 LRI
        "\xE2\x81\xA7", // U+2067 RLI
        "\xE2\x81\xA8", // U+2068 FSI
        "\xE2\x81\xA9", // U+2069 PDI
    };
    for (char const* sequence : kSequences) {
        std::size_t const length = std::char_traits<char>::length(sequence);
        if (index + length <= text.size() && text.compare(index, length, sequence) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

bool isValidUtf8(std::string_view text) noexcept {
    std::size_t i = 0;
    std::size_t const n = text.size();
    while (i < n) {
        std::uint8_t const byte = static_cast<std::uint8_t>(text[i]);
        std::size_t extra = 0;
        std::uint32_t codePoint = 0;
        if (byte < 0x80) {
            i += 1;
            continue;
        } else if ((byte & 0xE0) == 0xC0) {
            extra = 1;
            codePoint = byte & 0x1F;
        } else if ((byte & 0xF0) == 0xE0) {
            extra = 2;
            codePoint = byte & 0x0F;
        } else if ((byte & 0xF8) == 0xF0) {
            extra = 3;
            codePoint = byte & 0x07;
        } else {
            return false; // stray continuation or 5+-byte lead
        }
        if (i + extra >= n) {
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            std::uint8_t const cont = static_cast<std::uint8_t>(text[i + k]);
            if (!isContinuation(cont)) {
                return false;
            }
            codePoint = (codePoint << 6) | (cont & 0x3F);
        }
        // Reject overlong encodings, surrogates and out-of-range code points.
        if (extra == 1 && codePoint < 0x80) return false;
        if (extra == 2 && codePoint < 0x800) return false;
        if (extra == 3 && codePoint < 0x10000) return false;
        if (codePoint > 0x10FFFF) return false;
        if (codePoint >= 0xD800 && codePoint <= 0xDFFF) return false;
        i += extra + 1;
    }
    return true;
}

bool isSafeDisplayText(std::string_view text) noexcept {
    if (!isValidUtf8(text)) {
        return false;
    }
    for (std::size_t i = 0; i < text.size();) {
        std::uint8_t const byte = static_cast<std::uint8_t>(text[i]);
        if (byte < 0x20 || byte == 0x7F) {
            return false;
        }
        if (byte < 0x80) {
            if (byte == '/' || byte == '\\') {
                return false; // path separators are never valid in names
            }
            ++i;
            continue;
        }
        if (isBidiControlAt(text, i)) {
            return false;
        }
        // Skip a full UTF-8 sequence.
        std::size_t len = 1;
        if ((byte & 0xE0) == 0xC0) len = 2;
        else if ((byte & 0xF0) == 0xE0) len = 3;
        else if ((byte & 0xF8) == 0xF0) len = 4;
        i += len;
    }
    return true;
}

bool parseInt64String(std::string_view text, std::int64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    bool negative = false;
    std::size_t i = 0;
    if (text[0] == '-') {
        negative = true;
        i = 1;
        if (text.size() == 1) {
            return false;
        }
    } else if (text[0] == '+') {
        return false;
    }
    std::uint64_t magnitude = 0;
    std::uint64_t const limit = negative
        ? (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u)
        : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    for (; i < text.size(); ++i) {
        char const c = text[i];
        if (c < '0' || c > '9') {
            return false;
        }
        std::uint64_t const digit = static_cast<std::uint64_t>(c - '0');
        if (magnitude > (limit - digit) / 10) {
            return false; // overflow
        }
        magnitude = magnitude * 10 + digit;
    }
    if (negative) {
        if (magnitude == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u) {
            out = std::numeric_limits<std::int64_t>::min();
        } else {
            out = -static_cast<std::int64_t>(magnitude);
        }
    } else {
        out = static_cast<std::int64_t>(magnitude);
    }
    return true;
}

bool parseUint64String(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (char const c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        std::uint64_t const digit = static_cast<std::uint64_t>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

std::string toString(std::int64_t value) {
    return std::to_string(value);
}

std::string toString(std::uint64_t value) {
    return std::to_string(value);
}

bool isValidDisplayName(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaxDisplayNameBytes) {
        return false;
    }
    if (!isSafeDisplayText(name)) {
        return false;
    }
    char const last = name.back();
    if (last == '.' || last == ' ') {
        return false; // Windows-normalization hazard
    }
    return true;
}

} // namespace lholo::sync::validation
