// LHolo - Pure section visibility and scheduling rules.
#pragma once

#include "projection/core/ProjectionFrustum.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

namespace lholo::structure {
struct LoadedStructure;
}

namespace lholo::projection::detail {

// Exact cache key: turning, moving, projection changes and resource publication
// all invalidate classification. No camera/world object is retained.
struct ProjectionViewKey {
    std::array<float, 16> matrix{};
    float originX{}, originY{}, originZ{};
    std::uint64_t activation{}, meshRevision{};
    std::size_t sections{};
    bool hasMatrix{};
    bool operator==(ProjectionViewKey const&) const = default;
};

struct ProjectionBounds {
    Vec3 minimum{};
    Vec3 maximum{};
    bool valid{};

    void include(Vec3 const &point) {
        if (!valid) {
            minimum = maximum = point;
            valid = true;
            return;
        }
        minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
        maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
    }
};

// Transform cell origins, then add their unit extent; transforming boundary
// coordinates directly introduces a one-cell error on reversed axes.
ProjectionBounds projectionSectionBounds(std::tuple<int, int, int> const &key,
                                         structure::LoadedStructure const &loaded, int mirror, int rotation);

inline bool projectionBoundsVisible(ProjectionFrustum const &frustum, ProjectionBounds const &bounds,
                                    Vec3 const &origin, float padding = 0) {
    if (!bounds.valid)
        return true;
    return frustum.isBoxVisible({bounds.minimum.x + origin.x - padding, bounds.minimum.y + origin.y - padding,
                                 bounds.minimum.z + origin.z - padding},
                                {bounds.maximum.x + origin.x + padding, bounds.maximum.y + origin.y + padding,
                                 bounds.maximum.z + origin.z + padding});
}

inline bool projectionBoundsWithinDistance(
    ProjectionBounds const& bounds, Vec3 const& origin,float radius) noexcept {
    if(!bounds.valid || !std::isfinite(radius))return true;
    if(radius<0.0f)return false;
    float const x0=bounds.minimum.x+origin.x,x1=bounds.maximum.x+origin.x;
    float const z0=bounds.minimum.z+origin.z,z1=bounds.maximum.z+origin.z;
    float const dx=std::max({0.0f,x0,-x1});
    float const dz=std::max({0.0f,z0,-z1});
    return static_cast<double>(dx)*dx+static_cast<double>(dz)*dz
        <=static_cast<double>(radius)*radius;
}
struct ProjectionSectionVisibility {
    bool renderVisible{},buildVisible{},prefetchVisible{};
    bool withinBuildDistance{true},withinRetentionDistance{true};
};
// Native camera far clip is a conservative limit for CPU mesh streaming;
// invalid/unknown clip planes must never hide projection geometry.
inline ProjectionSectionVisibility classifyProjectionSection(
    ProjectionFrustum const& frustum,ProjectionBounds const& bounds,
    Vec3 const& origin,float farClip) {
    ProjectionSectionVisibility result;
    auto const eligible=std::isfinite(farClip) && farClip>=32.0f && farClip<=8192.0f;
    auto const radius=eligible?farClip:std::numeric_limits<float>::infinity();
    // The frustum intentionally contains only SIDE planes. Native mZFar
    // is not a safe HARD cutoff for schematics: the game's projection can
    // still be visible past it, leaving Missing comparison colors blank
    // at the far ends of large structures.
    //
    // Use the side frustum as the authority for drawing and mesh builds.
    // Distance is a hint ONLY for unseen prefetch and GPU residency. A
    // visible/fringe section must not have its transparent source retired.
    auto const inView=projectionBoundsVisible(frustum,bounds,origin,2.0f);
    auto const inPrefetchView=projectionBoundsVisible(frustum,bounds,origin,16.0f);
    result.renderVisible=inView;
    result.buildVisible=inView;
    result.prefetchVisible=inPrefetchView;
    result.withinBuildDistance=inPrefetchView
        || projectionBoundsWithinDistance(bounds,origin,radius+16.0f);
    result.withinRetentionDistance=inPrefetchView
        || projectionBoundsWithinDistance(bounds,origin,radius+32.0f);
    return result;
}
inline bool shouldRetireDistantTransparentMesh(
    bool withinRetentionDistance,bool dirty,bool inFlight,
    bool hasBlendSource,bool worldMutationActive) noexcept {
    return !withinRetentionDistance && !dirty && !inFlight
        && hasBlendSource && !worldMutationActive;
}
// Shared selection for compatibility liquid proxy draw calls. Bound all
// parallel section arrays before reading their elements; visibility is derived
// from the same fail-open frustum classification as regular block meshes.
// Retain the GPU resources for off-screen sections; this filters draws only.
template <class IsVisible, class IsValid>
std::vector<std::size_t> collectVisibleLiquidProxySections(
    std::size_t sectionCount,
    std::size_t meshCount,
    std::size_t cellCount,
    IsVisible&& isVisible,
    IsValid&& isValid
) {
    auto const count = (std::min)({sectionCount, meshCount, cellCount});
    std::vector<std::size_t> shown;
    for (std::size_t section = 0; section < count; ++section) {
        if (isVisible(section) && isValid(section)) shown.push_back(section);
    }
    return shown;
}

// Hidden edits must not starve initial geometry currently in view.
inline int projectionBuildRank(bool visible, bool prefetch, bool incremental) {
    return (visible ? 0 : prefetch ? 2 : 4) + (incremental ? 0 : 1);
}

struct ProjectionBuildCandidate {
    bool dirty{};
    bool inFlight{};
    bool visible{};
    bool prefetch{};
    bool incremental{};
    float distanceSquared{};
};

// One linear pass, no queue allocation or stale heap entries after camera moves.
// The eighth selection uses round-robin fairness instead of view priority.
template <class ReadCandidate>
std::optional<std::size_t> selectProjectionBuild(std::size_t count, std::uint64_t selections,
                                                 std::size_t cursor, ReadCandidate &&read) {
    bool const background = (selections + 1) % 8 == 0;
    std::optional<std::size_t> selected;
    int selectedRank = std::numeric_limits<int>::max();
    float selectedDistance = std::numeric_limits<float>::max();
    for (std::size_t offset = 0; offset < count; ++offset) {
        auto const index = background ? (cursor + offset) % count : offset;
        auto const candidate = read(index);
        if (!candidate.dirty || candidate.inFlight)
            continue;
        if (background)
            return index;
        auto const rank = projectionBuildRank(candidate.visible, candidate.prefetch, candidate.incremental);
        auto const distance = std::isfinite(candidate.distanceSquared) ? candidate.distanceSquared
                                                                       : std::numeric_limits<float>::max();
        if (!selected || rank < selectedRank || (rank == selectedRank && distance < selectedDistance)) {
            selected = index;
            selectedRank = rank;
            selectedDistance = distance;
        }
    }
    return selected;
}

} // namespace lholo::projection::detail
