#include "io/AtomicOutput.h"
#include "app/ScopeExit.h"

#include <atomic>
#include <system_error>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace lholo::io {
namespace {
[[noreturn]] void throwOutputError(char const* operation, DWORD error) {
    throw std::system_error(static_cast<int>(error), std::system_category(), operation);
}
}

OutputStagingFile::OutputStagingFile(std::filesystem::path const& output)
    : mDestination(std::filesystem::absolute(output)) {
    static std::atomic_uint64_t sequence{};
    bool owned{};
    for (int attempt = 0; attempt < 64; ++attempt) {
        mDirectory = mDestination.parent_path() / (L".lholo-output-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)));
        if (CreateDirectoryW(mDirectory.c_str(), nullptr)) { owned = true; break; }
        auto const error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) throwOutputError("create output staging directory", error);
    }
    if (!owned) throwOutputError("allocate output staging directory", ERROR_ALREADY_EXISTS);
    bool initialized{};
    app::ScopeExit rollback([&]() noexcept {
        if (!initialized && !RemoveDirectoryW(mDirectory.c_str())) {
            OutputDebugStringA("LHolo: output staging initialization cleanup failed\n");
        }
    });
    mStaged = mDirectory / mDestination.filename();
    initialized = true;
}

OutputStagingFile::~OutputStagingFile() {
    if (!DeleteFileW(mStaged.c_str())) {
        auto const error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            OutputDebugStringA("LHolo: output staging file cleanup failed\n");
        }
    }
    if (!RemoveDirectoryW(mDirectory.c_str())) OutputDebugStringA("LHolo: output staging directory cleanup failed\n");
}

void OutputStagingFile::commit() {
    auto const attributes = GetFileAttributesW(mStaged.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) throwOutputError("inspect staged output", GetLastError());
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        throwOutputError("staged output is not a regular file", ERROR_INVALID_DATA);
    }
    if (!MoveFileExW(mStaged.c_str(), mDestination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throwOutputError("replace output", GetLastError());
    }
}
} // namespace lholo::io
