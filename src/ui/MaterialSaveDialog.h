#pragma once
#include <filesystem>
#include <optional>
#include "io/MaterialExport.h"
namespace lholo::ui {
// Capture the translated dialog title on Present; the worker owns the string.
std::optional<std::filesystem::path> saveMaterialFile(std::wstring const& title,
                                                     io::MaterialExportCancel const& cancellation);
}
