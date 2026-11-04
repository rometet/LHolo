#pragma once

#include <filesystem>
#include <string>

// Long-path support. The §5.4 client cache and §5.2 object store layouts embed
// full UUIDs and SHA-256 names, which can exceed the legacy 260-character
// MAX_PATH limit when the mod is installed under a deep directory. Windows only
// accepts longer paths via the "\\?\" (or "\\?\UNC\") prefix, so all file I/O in
// the sync layer goes through ioPath().

namespace lholo::sync::paths {

[[nodiscard]] inline std::filesystem::path ioPath(std::filesystem::path const& path) {
    std::error_code code;
    std::filesystem::path absolute = path.is_absolute() ? path : std::filesystem::absolute(path, code);
    if (code) {
        return path;
    }
    auto const native = absolute.native();
    auto const prefix = std::wstring{L"\\\\?\\"};
    if (native.rfind(prefix, 0) == 0) {
        return absolute;
    }
    if (native.rfind(L"\\\\", 0) == 0) {
        // UNC path: \\server\share -> \\?\UNC\server\share
        return std::filesystem::path(std::wstring{L"\\\\?\\UNC\\"} + native.substr(2));
    }
    return std::filesystem::path(prefix + native);
}

// Lexical containment check for paths built only from validated components
// (hex hashes / canonical UUIDs). Avoids canonicalisation (which can fail on
// over-long paths) while still rejecting traversal.
[[nodiscard]] inline bool lexicallyContained(
    std::filesystem::path const& root,
    std::filesystem::path const& candidate
) {
    std::error_code code;
    auto const rel = std::filesystem::relative(candidate, root, code);
    if (code) {
        return false;
    }
    if (rel.empty()) {
        return false;
    }
    auto const text = rel.generic_string();
    if (text.rfind("..", 0) == 0) {
        return false;
    }
    return true;
}

} // namespace lholo::sync::paths
