#pragma once

#include <filesystem>
#include <optional>
#include <cstddef>

namespace lholo::ui {

// Keep the default Windows file-picker filter aligned with the structure
// parser and the schematic library's accepted extensions.
inline constexpr char StructureOpenFilePatterns[] = "*.mcstructure;*.litematic;*.lholo";
// Windows' common dialog can retain a narrower extension filter than our
// combined extension list. The default wildcard makes .lholo visible on the
// very first Browse without requiring the user to select "All files".
// The parser remains responsible for rejecting unsupported formats.
inline constexpr char StructureOpenDefaultPatterns[] = "*.*";
inline constexpr std::size_t FileDialogPathCodeUnitCapacity = 32768;
// One UTF-16 code unit needs at most three UTF-8 bytes; a surrogate pair
// needs four. Keep the entire selected dialog path, including its terminator.
inline constexpr std::size_t StructurePathUtf8Capacity = (FileDialogPathCodeUnitCapacity - 1U) * 3U + 1U;

std::optional<std::filesystem::path> openStructureFile(std::filesystem::path const& current);
std::optional<std::filesystem::path> saveMcstructureFile();

} // namespace lholo::ui
