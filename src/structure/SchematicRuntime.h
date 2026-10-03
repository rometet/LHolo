#pragma once
#include "structure/PlacementSession.h"
#include "structure/Verification.h"
#include "structure/VerificationSelection.h"
#include "structure/ManualVerificationControl.h"
#include "structure/ActiveProjection.h"
#include <map>
#include <memory>
#include <optional>

class LocalPlayer;
namespace lholo::structure::schematic {
struct MaterialRow {
    std::string item;
    MaterialCount count;
    std::optional<int> inventory;
};
struct Report {
    VerificationTally tally;
    std::vector<Mismatch> mismatches; // 512 Missing + 512 WrongState + 512 WrongType/Extra, then sorted
    std::vector<MaterialRow> materials;
    bool running{}, truncated{};
    std::uint64_t checked{};
    float progress{};
    ReportStamp stamp;
};
struct Snapshot {
    PlacementSessionSnapshot session;
    std::vector<std::string> files;
    std::shared_ptr<Report const> report;
    std::optional<Mismatch> target;
    bool worldAvailable{};
    int dimension{};
    Cell feet;
    std::string library, status;
    MistakeFilter filter{MistakeFilter::Mistakes};
    VerificationPhase phase{VerificationPhase::NotVerified};
    bool activeProjectionAvailable{};
};
Snapshot snapshot();
void refreshFiles();
bool place(std::string const& file);
bool importSavedProjection();
bool select(std::uint64_t id);
bool erase(std::uint64_t id);
bool edit(SavedPlacement const& placement, std::uint64_t revision);
bool moveToFeet(std::uint64_t id);
void verify();
void cancelVerification();
void cycleMistake(MistakeFilter filter);
bool selectMistake(ReportStamp const& stamp,std::size_t index);
void setMistakeFilter(MistakeFilter filter);
void clearMistakeTarget();
// Caller passes the already validated Current projection context. This getter
// only takes the schematic mutex and never enters projection/session locks.
std::optional<SelectedMistake> highlightTarget(std::uint64_t worldEpoch,int dimension,
    std::uint64_t loadedGeneration) noexcept;
// Only the existing LocalPlayer tick and Present control plane invoke these.
void tick(LocalPlayer& player);
void processControl();
void reset();
// Called by the existing material shutdown after callback drain.
void shutdown();
void rememberSelectedTransform();
ProjectionRequestStamp beginProjectionRequest(std::uint64_t placementId=0);
void failProjectionRequest(ProjectionRequestStamp const& request) noexcept;
void publishProjectionActivation(ActiveProjectionEvent event) noexcept;
void retireActiveProjection(bool preserveQueuedVerification=false) noexcept;
} // namespace lholo::structure::schematic
