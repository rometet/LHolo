#pragma once

#include "structure/PlacementTransform.h"
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace lholo::structure {
inline constexpr std::size_t kMaxPlacements = 64, kMaxPlacementDocument = 131072;
struct SavedPlacement {
    std::uint64_t id{};
    std::string name, file;
    int dimension{};
    Cell origin;
    int rotation{}, mirror{};
    bool visible{true}, countExtras{true};
    LayerAxis layerAxis{LayerAxis::BottomToTop};
    LayerDisplayMode layerMode{LayerDisplayMode::All};
    int layer{};
    bool operator==(SavedPlacement const&) const = default;
};
struct PlacementDocument {
    std::vector<SavedPlacement> placements;
    std::uint64_t selected{}; // stable ID; zero means explicitly no selection
    bool operator==(PlacementDocument const&) const = default;
};
inline SavedPlacement const* selectedPlacement(PlacementDocument const& d) {
    auto const it=std::find_if(d.placements.begin(),d.placements.end(),[&](auto const& p){return p.id==d.selected;});
    return it==d.placements.end()?nullptr:&*it;
}
inline bool selectPlacement(PlacementDocument& d,std::uint64_t id) {
    if(id && std::none_of(d.placements.begin(),d.placements.end(),[&](auto const& p){return p.id==id;}))return false;
    d.selected=id;return true;
}
inline bool removePlacement(PlacementDocument& d,std::uint64_t id) {
    if(!std::erase_if(d.placements,[&](auto const& p){return p.id==id;}))return false;
    if(d.selected==id)d.selected=0;return true;
}
bool safeSchematicPath(std::string_view relative);
// Also checks canonical containment (including directory symlinks/junctions).
std::filesystem::path resolveSchematicPath(std::filesystem::path const& root, std::string_view relative);
std::string placementWorldKey(std::string_view stableId);
std::string placementServerKey(std::string_view address, unsigned port);
std::string encodePlacements(PlacementDocument const& document);
PlacementDocument decodePlacements(std::string_view text);

struct PlacementSessionSnapshot {
    PlacementDocument document;
    std::uint64_t revision{};
    bool writable{};
    std::string status;
};
// Domain-only owner. No engine pointers, renderer objects, or UI objects.
// A load failure locks this document against writes for its entire binding.
class PlacementSession {
public:
    void bind(std::filesystem::path path);
    void bindTransient();
    PlacementSessionSnapshot snapshot() const;
    bool replace(PlacementDocument document, std::uint64_t expectedRevision);
    void clear();
private:
    mutable std::mutex mMutex;
    std::filesystem::path mPath;
    PlacementDocument mDocument;
    std::uint64_t mRevision{};
    bool mWritable{};
    std::string mStatus;
};
} // namespace lholo::structure
