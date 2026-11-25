#pragma once
#include <filesystem>
#include <optional>
#include "io/MaterialExport.h"
namespace lholo::ui {
// Capture the translated dialog title on Present; the worker owns the string.
// The format picks the suggested file name, filter list and default extension;
// the user can still type any name, so the writer must stay format-agnostic on
// the extension and only the request.format decides the body.
std::optional<std::filesystem::path> saveMaterialFile(std::wstring const& title,
                                                     io::MaterialExportCancel const& cancellation,
                                                     io::MaterialExportFormat format);
}
