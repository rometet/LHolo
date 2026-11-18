// LHolo / upstream-ed9944f glass-liquid face interleaving adapted for the
// independently maintained 26.60 Exact Replay and Glass R5 native streams.
// Only compatible, modest-sized, mixed sections use this path; all others
// retain the proven original GPU and liquid replay fallback.
#pragma once
#include "projection/core/ProjectionState.h"
#include "projection/core/ProjectionTransparentOrder.h"
#include "projection/core/ProjectionLiquidCompatColor.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/deps/renderer/Camera.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>
#include <glm/geometric.hpp>

namespace lholo::projection::detail {

inline bool glassLiquidMergedReady(ProjectionState const& state, std::size_t index) {
    if (index >= state.sections.size() || index >= state.praxisCompatLiquidSections.size())
        return false;
    auto const& section = state.sections[index];
    auto const& liquid = state.praxisCompatLiquidSections[index];
    auto const* source = section.blendSource.get();
    return source && liquid && liquid->ready() && liquid->nativeStream
        && section.mixedGlassLiquid && section.mixedGlassLiquid->mesh
        && section.mixedGlassLiquid->mesh->isValid()
        && section.mixedGlassLiquid->glassSource == source
        && section.mixedGlassLiquid->liquidSource == liquid->nativeStream.get()
        && section.mixedGlassLiquid->uploadedRevision == section.uploadedRevision;
}

struct GlassLiquidMergeFace {
    mce::MeshData const* data{};
    std::vector<std::uint32_t> const* colors{};
    std::size_t first{};
};

inline bool validateGlassLiquidSource(mce::MeshData const& data,
                                     std::vector<std::uint32_t> const* derived = nullptr) {
    auto const count = data.mPositions.get().size();
    if (!count || count % 4 || data.mMode != mce::PrimitiveMode::QuadList
        || !data.mIndices.get().empty()
        || data.mTextureUVs[0].get().size() != count
        || (!data.mColors.get().empty() && data.mColors.get().size() != count)
        || (!data.mNormals.get().empty() && data.mNormals.get().size() != count)
        || (derived && derived->size() != count))
        return false;
    for (auto const& p : data.mPositions.get())
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    return true;
}

// Frame budget, not a recurring task or asynchronous operation. Keeps camera
// turns on huge schematics responsive even if many mixed sections are visible.
inline void prepareMixedGlassLiquidMeshes(
    ProjectionState& state, BaseActorRenderContext& context,
    std::size_t budget = 3
) {
    if (state.sections.empty()) return;
    auto service = context.mScreenContext.tessellator.mBufferResourceService.lock();
    if (!service) return;
    auto const& cam = context.mScreenContext.camera;
    auto const clip = cam.getProjectionMatrix() * *cam.viewMatrixStack->top()._m
        * *cam.worldMatrixStack->top()._m;
    std::array<float, 3> const axis{clip[0][3],clip[1][3],clip[2][3]};
    if (!std::all_of(axis.begin(),axis.end(),[](float v){return std::isfinite(v);}))
        return;
    // Bound extra GPU residency across thousands of large schematic
    // sections. Off-screen mixed buffers are retired immediately; native
    // glass/liquid sources remain available as the fallback.
    constexpr std::size_t MaxMergedVertices = 160000;
    constexpr std::size_t MaxResidentMergedVertices = 2000000;
    std::size_t residentVertices{};
    for (std::size_t i=0; i<state.sections.size(); ++i) {
        auto& sec=state.sections[i];
        if (!sec.renderVisible) {
            sec.mixedGlassLiquid.reset();
        } else if (sec.mixedGlassLiquid && !glassLiquidMergedReady(state,i)) {
            sec.mixedGlassLiquid.reset();
        } else if (glassLiquidMergedReady(state,i)) {
            residentVertices += sec.mixedGlassLiquid->vertexCount;
        }
    }
    auto const total = state.sections.size();
    std::size_t work{};
    for (std::size_t inspected = 0; inspected < total && work < budget; ++inspected) {
        auto const index = state.mixedGlassLiquidCursor++ % total;
        auto& section = state.sections[index];
        if (!section.renderVisible || !section.blendSource
            || index >= state.praxisCompatLiquidSections.size())
            continue;
        auto const& liquid = state.praxisCompatLiquidSections[index];
        if (!liquid || !liquid->ready() || !liquid->nativeStream) continue;
        auto const& terrain = *section.blendSource;
        auto const& fluid = *liquid->nativeStream;
        if (!validateGlassLiquidSource(terrain)
            || !validateGlassLiquidSource(fluid,&liquid->derivedColors))
            continue;
        auto const count = terrain.mPositions.get().size()+fluid.mPositions.get().size();
        auto const ownResident = glassLiquidMergedReady(state,index)
            ? section.mixedGlassLiquid->vertexCount : 0;
        if (count > MaxMergedVertices
            || residentVertices-ownResident+count > MaxResidentMergedVertices
            || count/4 > static_cast<std::size_t>(std::numeric_limits<int>::max()/4))
            continue;
        if (!section.mixedGlassLiquid)
            section.mixedGlassLiquid = std::make_unique<MixedGlassLiquidMesh>();
        auto& cache = *section.mixedGlassLiquid;
        auto const changed = cache.glassSource != &terrain
            || cache.liquidSource != &fluid
            || cache.uploadedRevision != section.uploadedRevision;
        if (!changed && cache.axisValid && cache.axis == axis && cache.mesh) continue;
        ++work;
        std::vector<GlassLiquidMergeFace> sources;
        std::vector<ProjectionTransparentFaceKey> keys;
        sources.reserve(count/4);
        keys.reserve(count/4);
        auto append = [&](mce::MeshData const& source,
                          std::vector<std::uint32_t> const* colors, bool isLiquid) {
            auto const& positions = source.mPositions.get();
            for (std::size_t first = 0; first < positions.size(); first += 4) {
                ProjectionTransparentFaceKey key;
                key.id = (static_cast<std::uint64_t>(isLiquid) << 32) | first / 4;
                for (std::size_t k = 0; k < 4; ++k) {
                    auto const& pos = positions[first+k];
                    key.center[0] += pos.x*0.25f;
                    key.center[1] += pos.y*0.25f;
                    key.center[2] += pos.z*0.25f;
                }
                keys.push_back(key);
                sources.push_back({&source, colors, first});
            }
        };
        append(terrain,nullptr,false);
        append(fluid,&liquid->derivedColors,true);
        auto order = projectionTransparentOrder(keys,axis);
        if (!changed && cache.mesh && order == cache.order) {
            cache.axis = axis;
            cache.axisValid = true;
            continue;
        }
        if (order.empty()) continue;
        Tessellator builder{context.mScreenContext.tessellator.mBufferResourceService};
        builder.begin(Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList, static_cast<int>(order.size()*4), false);
        builder.mHasNormals = true;
        for (auto selected : order) {
            auto const& face = sources[selected];
            auto const& data = *face.data;
            auto const& positions = data.mPositions.get();
            auto const& normals = data.mNormals.get();
            auto const& uv = data.mTextureUVs[0].get();
            auto const& nativeColors = data.mColors.get();
            auto generatedNormal = glm::cross(positions[face.first+1]-positions[face.first],
                positions[face.first+2]-positions[face.first]);
            auto const normalLength = glm::length(generatedNormal);
            if (normalLength > 0.0f) generatedNormal /= normalLength;
            else generatedNormal = glm::vec3{0.0f,1.0f,0.0f};
            for (std::size_t corner = 0; corner < 4; ++corner) {
                auto const at = face.first + corner;
                auto const& pos = positions[at];
                builder.mNextColor.get() = face.colors ? (*face.colors)[at]
                    : nativeColors.empty() ? 0xFFFFFFFFU : nativeColors[at];
                if (!normals.empty()) {
                    auto const& normal = normals[at];
                    builder.mNextNormal.get() = glm::vec4{normal.x,normal.y,normal.z,0.0f};
                } else {
                    builder.mNextNormal.get() = glm::vec4{generatedNormal,0.0f};
                }
                builder.mNextUV[0].get() = uv[at];
                builder.vertex(pos.x,pos.y,pos.z);
            }
        }
        auto replacement = std::make_unique<mce::Mesh>(builder.end(
            Tessellator::UploadMode::Buffered,"LHoloGlassLiquidInterleaved",
            SupplementaryFieldAutoGenerationMode{0}));
        if (!replacement->isValid()) continue;
        cache.mesh = std::move(replacement);
        cache.order = std::move(order);
        cache.axis = axis;
        cache.axisValid = true;
        cache.glassSource = &terrain;
        cache.liquidSource = &fluid;
        cache.uploadedRevision = section.uploadedRevision;
        cache.vertexCount = count;
        residentVertices = residentVertices - ownResident + count;
    }
}
} // namespace lholo::projection::detail
