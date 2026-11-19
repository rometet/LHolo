#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "sync/common/SharedTypes.h"

// Friendly names are for the user's local library only. The network cache
// remains content-addressed (SHA-256); never trust the remote name as a path.
namespace lholo::sync {

inline std::string schematicExtension(SchematicFormat format) {
    switch (format) {
        case SchematicFormat::Litematic: return ".litematic";
        case SchematicFormat::Mcstructure: return ".mcstructure";
        default: return ".bin";
    }
}

inline std::string friendlySchematicFilename(
    std::string_view displayName, SchematicFormat format, Sha256Hash const& hash
) {
    std::string const extension = schematicExtension(format);
    std::string stem{displayName};

    // The catalog may contain either "Bartering" or "Bartering.litematic".
    if (stem.size() >= extension.size()) {
        auto tail = std::string_view{stem}.substr(stem.size() - extension.size());
        bool const matches = std::equal(tail.begin(), tail.end(), extension.begin(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
        if (matches) stem.resize(stem.size() - extension.size());
    }

    // Do not turn UTF-8 display names into a Windows path or allow traversal.
    // Retain Unicode bytes; replace forbidden ASCII characters only.
    for (char& ch : stem) {
        auto const byte = static_cast<unsigned char>(ch);
        if (byte < 32 || byte == 127 || std::string_view{"\\/:*?\"<>|"}.find(ch) != std::string_view::npos) {
            ch = '_';
        }
    }
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();

    // Keep the final file name comfortably below Windows and LHolo limits.
    if (stem.size() > 108) {
        std::size_t count = 108;
        while (count && (static_cast<unsigned char>(stem[count]) & 0xc0) == 0x80) --count;
        stem.resize(count);
    }

    // Win32 device names are reserved even with an extension.
    std::string upper;
    for (unsigned char ch : stem) upper += static_cast<char>(std::toupper(ch));
    auto const base = upper.substr(0, upper.find('.'));
    bool const device = base == "CON" || base == "PRN" || base == "AUX" || base == "NUL"
        || (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT"))
            && base[3] >= '1' && base[3] <= '9');
    bool hashLike = stem.size() == 64;
    for (unsigned char ch : stem) {
        if (!std::isxdigit(ch)) { hashLike = false; break; }
    }
    if (stem.empty() || stem == "." || stem == ".." || device || hashLike) {
        stem = "Shared-" + hash.toHex().substr(0, 12);
    }
    return stem + extension;
}

} // namespace lholo::sync
