#pragma once
#include "structure/MaterialList.h"
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <chrono>
#include <mutex>

namespace lholo::io {
struct MaterialExportRequest {
    std::shared_ptr<structure::detail::MaterialListSnapshot const> snapshot;
    structure::detail::MaterialFilter filter{};
    // Render/localization is captured once on explicit operation; no engine
    // objects or mutable UI strings cross the worker boundary.
    std::vector<std::string> names;
    std::string capturedAt;
};
enum class MaterialExportPhase { Idle, Saving, Saved, Cancelled, Failed };
struct MaterialExportResult {
    MaterialExportPhase phase{MaterialExportPhase::Idle};
    std::filesystem::path destination;
    std::string error;
    structure::detail::MaterialListScope scope;
    std::string source;
    bool accepting{true};
};
struct MaterialExportCancellation {
    std::atomic_bool requested{};
    void cancel() noexcept { requested.store(true, std::memory_order_release); }
    bool cancelled() const noexcept { return requested.load(std::memory_order_acquire); }
};
using MaterialExportCancel = std::shared_ptr<MaterialExportCancellation>;
bool writeMaterialTsv(std::filesystem::path const&, MaterialExportRequest const&,
                      MaterialExportCancel const& cancellation = {});
// Owns the worker until completion. Never detach or discard a running future.
// closeAndDrain must succeed BEFORE callback teardown or DLL unload. A timeout
// leaves the owned job resident and admission closed; the loader must refuse
// disable/unload and retry after cancellation or the outstanding IO completes.
class MaterialExportJob {
public:
    using ChooseDestination = std::function<std::optional<std::filesystem::path>(MaterialExportCancel const&)>;
    bool start(MaterialExportRequest request, ChooseDestination choose);
    MaterialExportResult poll();
    bool closeAndDrain(std::chrono::milliseconds budget);
    bool openSession();
private:
    void pollLocked();
    std::mutex mMutex;
    std::shared_future<MaterialExportResult> mFuture;
    MaterialExportCancel mCancellation;
    bool mAccepting{true};
    MaterialExportResult mResult;
};
MaterialExportJob& materialExportJob();
} // namespace lholo::io
