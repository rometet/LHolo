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
namespace lholo::structure {struct LoadedStructure;}
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
    // Present only for the current selected mcstructure whose file has an
    // explicit, valid structure_world_origin tag.
    std::optional<Cell> sourceWorldOrigin;
    std::string selectedRegion; // transient UI selection, independent of visibility
};
Snapshot snapshot();
// A completed manual verifier report, validated against the SAME active
// world/placement/filter as the verifier tab. Snapshot totals are intentionally
// stable until the user verifies again. Never expose a running partial report.
std::optional<VerificationTally> completedVerifierTallyForHud();
void refreshFiles();
bool place(std::string const& file);
bool importSavedProjection();
bool select(std::uint64_t id);
bool erase(std::uint64_t id);
bool edit(SavedPlacement const& placement, std::uint64_t revision);
// Selecting a region never changes its visible state or requests a mesh rebuild.
bool chooseRegion(std::uint64_t placementId, std::string const& name);
struct RegionHighlight { Cell min, max; };
std::optional<RegionHighlight> highlightRegion(
    std::uint64_t worldEpoch, int dimension, std::uint64_t loadedGeneration,
    LoadedStructure const& loaded) noexcept;
// Pre-publication, main-thread filtering shared by rendering, EasyPlace,
// the manual verifier and material HUD. No engine/UI pointers are retained.
void filterRegionsForActivation(LoadedStructure& loaded,ProjectionRequestStamp const& request);
bool moveToFeet(std::uint64_t id);
bool moveToSourceOrigin(std::uint64_t id);
void verify();
void cancelVerification();
// Discard only the scan result/selection. Does not alter placement or projection.
void resetVerification();
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
