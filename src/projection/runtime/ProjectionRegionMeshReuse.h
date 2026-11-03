#pragma once

#include <cstddef>
#include <memory>

namespace lholo::structure { struct LoadedStructure; }
namespace lholo::projection::detail {
struct ProjectionState;

// Must be called on the render owner after the old worker has drained.
// Fail closed unless the world, full source and original placement match.
std::unique_ptr<struct RegionMeshRetention> captureRegionMeshReuse(
    ProjectionState& oldState,
    std::shared_ptr<structure::LoadedStructure const> const& incoming);

// Called once after the new virtual world is built and before correction
// scanning/mesh work. Never crosses a renderer or resource-pack reload.
std::size_t restoreUnchangedRegionMeshes(ProjectionState& newState) noexcept;
}
