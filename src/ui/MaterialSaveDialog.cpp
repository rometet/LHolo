#include "ui/MaterialSaveDialog.h"
#include "ui/FileDialog.h"
#include <Windows.h>
#include <commdlg.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace lholo::ui {
std::optional<std::filesystem::path> saveMaterialFile(std::wstring const& title) {
    std::vector<wchar_t> buffer(FileDialogPathCodeUnitCapacity, L'\0');
    constexpr wchar_t name[]=L"LHolo-materials.tsv";
    std::copy(std::begin(name),std::end(name),buffer.begin());
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);
    dialog.lpstrFile=buffer.data();dialog.nMaxFile=static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle=title.c_str();dialog.lpstrFilter=L"TSV (UTF-8)\0*.tsv\0\0";
    dialog.nFilterIndex=1;dialog.lpstrDefExt=L"tsv";
    dialog.Flags=OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&dialog)) {
        if (auto const error=CommDlgExtendedError()) throw std::runtime_error("Save dialog error " + std::to_string(error));
        return std::nullopt;
    }
    return std::filesystem::path(buffer.data());
}
}
