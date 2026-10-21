#include "ui/MaterialSaveDialog.h"
#include "ui/FileDialog.h"
#include <Windows.h>
#include <commdlg.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace lholo::ui {
namespace {
constexpr UINT_PTR CancelTimer = 1;
struct DialogContext { io::MaterialExportCancel cancellation; };
// Executed only on the dialog's owning worker. The hook child and its timer
// die before GetSaveFileNameW returns; context stays alive on that worker's
// stack. No cross-thread HWND ownership, detached callback, or engine borrow.
UINT_PTR CALLBACK cancelHook(HWND child, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* context = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(child, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        auto const* dialog = reinterpret_cast<OPENFILENAMEW const*>(lparam);
        context = reinterpret_cast<DialogContext*>(dialog->lCustData);
        SetWindowLongPtrW(child, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
        SetTimer(child, CancelTimer, 25, nullptr);
    }
    if ((message == WM_INITDIALOG || (message == WM_TIMER && wparam == CancelTimer))
        && context && context->cancellation && context->cancellation->cancelled()) {
        // Post so initialization can finish and the common dialog can unwind
        // its modal loop normally. If a nested prompt/IO delays that unwind,
        // closeAndDrain times out and the loader keeps this DLL resident.
        PostMessageW(GetParent(child), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
    }
    if (message == WM_DESTROY) {
        KillTimer(child, CancelTimer);
        SetWindowLongPtrW(child, GWLP_USERDATA, 0);
    }
    return 0;
}
}
std::optional<std::filesystem::path> saveMaterialFile(std::wstring const& title,
                                                     io::MaterialExportCancel const& cancellation) {
    if (cancellation && cancellation->cancelled()) return std::nullopt;
    DialogContext context{cancellation};
    std::vector<wchar_t> buffer(FileDialogPathCodeUnitCapacity, L'\0');
    constexpr wchar_t name[]=L"LHolo-materials.tsv";
    std::copy(std::begin(name),std::end(name),buffer.begin());
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);
    dialog.lpstrFile=buffer.data();dialog.nMaxFile=static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle=title.c_str();dialog.lpstrFilter=L"TSV (UTF-8)\0*.tsv\0\0";
    dialog.nFilterIndex=1;dialog.lpstrDefExt=L"tsv";
    dialog.Flags=OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_ENABLEHOOK;
    dialog.lpfnHook=cancelHook;dialog.lCustData=reinterpret_cast<LPARAM>(&context);
    if (!GetSaveFileNameW(&dialog)) {
        if (auto const error=CommDlgExtendedError()) throw std::runtime_error("Save dialog error " + std::to_string(error));
        return std::nullopt;
    }
    if (cancellation && cancellation->cancelled()) return std::nullopt;
    return std::filesystem::path(buffer.data());
}
}
