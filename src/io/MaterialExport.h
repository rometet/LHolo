#pragma once
#include "structure/MaterialList.h"
#include <filesystem>
#include <functional>
#include <future>
#include <memory>

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
};
void writeMaterialTsv(std::filesystem::path const&, MaterialExportRequest const&);
// A single finite job. Save-dialog/filesystem calls execute only on the worker.
// poll() never waits. shutdown() is called after native callback drain.
class MaterialExportJob {
public:
    using ChooseDestination = std::function<std::optional<std::filesystem::path>()>;
    bool start(MaterialExportRequest request, ChooseDestination choose);
    MaterialExportResult poll();
    void shutdown();
private:
    std::optional<std::future<MaterialExportResult>> mFuture;
    MaterialExportResult mResult;
};
MaterialExportJob& materialExportJob();
} // namespace lholo::io
