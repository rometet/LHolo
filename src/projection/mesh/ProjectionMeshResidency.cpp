#include "projection/mesh/ProjectionMeshResidency.h"
#include "projection/core/ProjectionState.h"
#include "projection/core/ProjectionResidencyBudget.h"

#include <algorithm>
#include <vector>

namespace lholo::projection::detail {
namespace {
float distanceSquared(ProjectionState const& state, std::size_t index, Vec3 const& camera) {
    auto const& center = state.sections[index].center;
    auto x = center.x + static_cast<float>(state.anchor.x + state.cachedOffsetX) - camera.x;
    auto y = center.y + static_cast<float>(state.anchor.y + state.cachedOffsetY) - camera.y;
    auto z = center.z + static_cast<float>(state.anchor.z + state.cachedOffsetZ) - camera.z;
    return x*x + y*y + z*z;
}
}
bool canBuildResidentSection(ProjectionState const& state, std::size_t section, Vec3 const& camera) {
    return !state.sections[section].residencyDeferred && (!state.residencyDistanceLimit ||
        distanceSquared(state, section, camera) <= *state.residencyDistanceLimit);
}
void recordProjectionResidentBytes(ProjectionState& state, std::size_t index) {
    auto& section = state.sections[index];
    std::size_t bytes{};
    auto count = [&](auto const& mesh) {
        if (!mesh) return;
        // Include retained CPU attributes and a conservative GPU allowance
        // even when upload has released the source vertex arrays.
        auto vertices = mesh->mVertexCount.get().value_or(0u);
        auto cpu = mesh->mMeshData.get().size();
        bytes += std::max<std::size_t>(cpu, static_cast<std::size_t>(vertices) * 64) * 2;
    };
    for (auto const& mesh : section.meshes) count(mesh);
    if (section.mixedGlassLiquid) count(section.mixedGlassLiquid->mesh);
    // The immutable CPU blend source keeps typed per-field vectors for the
    // camera-keyed resort; the same per-vertex allowance applies.
    if (section.blendSource)
        bytes += static_cast<std::size_t>(section.blendSource->mPositions.get().size()) * 64 * 2;
    if (index < state.warningFillSectionMeshes.size()) count(state.warningFillSectionMeshes[index]);
    if (index < state.correctionOutlineSectionMeshes.size()) count(state.correctionOutlineSectionMeshes[index]);
    if (index < state.wrongFillSectionMeshes.size()) count(state.wrongFillSectionMeshes[index]);
    if (index < state.wrongOutlineSectionMeshes.size()) count(state.wrongOutlineSectionMeshes[index]);
    if (index < state.blockEntityPlaceholderSectionMeshes.size()) count(state.blockEntityPlaceholderSectionMeshes[index]);
    if (index < state.nativeLiquidSectionMeshes.size()) count(state.nativeLiquidSectionMeshes[index]);
    if (index < state.liquidProxySectionMeshes.size()) count(state.liquidProxySectionMeshes[index]);
    section.residentBytes = bytes;
}

void trimProjectionMeshResidency(ProjectionState& state, Vec3 const& camera) {
    auto squared = [](Vec3 const& p) { return p.x * p.x + p.y * p.y + p.z * p.z; };
    bool moved = !state.hasResidencyCamera || squared(camera - state.residencyCamera) >= 64.0f;
    bool changed = state.residencyTrimRevision != state.meshUploadRevision || moved;
    if (!changed) return;
    // Retry deferred sections after moving one half-section. A stationary
    // camera must not rebuild/evict the same distant meshes every frame.
    if (moved) {
        state.residencyCamera = camera;
        state.hasResidencyCamera = true;
        state.residencyDistanceLimit.reset();
    }
    std::vector<ProjectionResident> residents;
    std::size_t total{};
    for (std::size_t i = 0; i < state.sections.size(); ++i) {
        auto& section = state.sections[i];
        if (moved) section.residencyDeferred = false;
        if (!section.residentBytes) continue;
        total += section.residentBytes;
        residents.push_back({i, section.residentBytes, distanceSquared(state, i, camera)});
    }
    auto evicted = selectProjectionEvictions(residents, ProjectionGeometryBudget);
    for (auto id : evicted) {
        auto& section = state.sections[id];
        for (auto& mesh : section.meshes) mesh.reset();
        section.blendSource.reset();
        section.blendSortKeyValid = false;
        section.mixedGlassLiquid.reset();
        if (id < state.warningFillSectionMeshes.size()) state.warningFillSectionMeshes[id].reset();
        if (id < state.correctionOutlineSectionMeshes.size()) state.correctionOutlineSectionMeshes[id].reset();
        if (id < state.wrongFillSectionMeshes.size()) state.wrongFillSectionMeshes[id].reset();
        if (id < state.wrongOutlineSectionMeshes.size()) state.wrongOutlineSectionMeshes[id].reset();
        if (id < state.blockEntityPlaceholderSectionMeshes.size()) state.blockEntityPlaceholderSectionMeshes[id].reset();
        if (id < state.nativeLiquidSectionMeshes.size()) state.nativeLiquidSectionMeshes[id].reset();
        if (id < state.praxisCompatLiquidSections.size()) state.praxisCompatLiquidSections[id].reset();
        if (id < state.liquidProxySectionMeshes.size()) state.liquidProxySectionMeshes[id].reset();
        section.residentBytes = 0;
        section.residencyDeferred = true;
        section.dirty = section.modelDirty = true;
        section.uploadedRevision = 0;
        ++section.requestedRevision;
        ++state.meshUploadRevision;
    }
    if (!evicted.empty()) {
        // Batched CPU sources referenced the evicted meshes; the caches
        // rebuild lazily on the next draw, exactly like invalidation does.
        state.correctionDrawBatches.clear();
        state.transparentDrawBatches.clear();
        // Once full, do not spend the worker building more distant sections
        // only to throw them away. A camera move reopens admission above.
        float farthest{};
        for (auto const& resident : residents)
            if (state.sections[resident.id].residentBytes)
                farthest = std::max(farthest, resident.distanceSquared);
        state.residencyDistanceLimit = farthest;
    } else if (total < ProjectionGeometryBudget * 3 / 4) {
        if (state.residencyDistanceLimit) {
            state.residencyDistanceLimit.reset();
            for (auto& section : state.sections) section.residencyDeferred = false;
        }
    }
    state.residencyTrimRevision = state.meshUploadRevision;
}
} // namespace lholo::projection::detail
