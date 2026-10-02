#pragma once
#include "structure/PlacementSession.h"
#include "structure/Verification.h"
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
    std::vector<Mismatch> mismatches; // nearest 512 per filter, sorted at job completion
    std::vector<MaterialRow> materials;
    bool running{}, truncated{};
    std::uint64_t checked{};
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
void cycleMistake(MistakeFilter filter);
// Only the existing LocalPlayer tick and Present control plane invoke these.
void tick(LocalPlayer& player);
void processControl();
void reset();
// Called by the existing material shutdown after callback drain.
void shutdown();
void rememberSelectedTransform();
} // namespace lholo::structure::schematic
