#pragma once

#include <filesystem>
#include <functional>
#include <utility>

namespace lholo::io {
class OutputStagingFile {
public:
    explicit OutputStagingFile(std::filesystem::path const& destination);
    ~OutputStagingFile();
    OutputStagingFile(OutputStagingFile const&) = delete;
    OutputStagingFile& operator=(OutputStagingFile const&) = delete;
    std::filesystem::path const& path() const noexcept { return mStaged; }
    void commit();
private:
    std::filesystem::path mDestination, mDirectory, mStaged;
};

// A native writer keeps its ordinary filename/extension inside an exclusively
// owned sibling directory. Replace the destination only after it succeeds.
template <class Writer>
bool writeOutputAtomically(std::filesystem::path const& output, Writer&& writer) {
    OutputStagingFile staging{output};
    if (!std::invoke(std::forward<Writer>(writer), staging.path())) return false;
    staging.commit();
    return true;
}
} // namespace lholo::io
