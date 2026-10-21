// LHolo - native waterlogged solid-face occlusion for Exact Replay.
// Product path is fail-closed, section-local and CPU-only.
#include "projection/mesh/WaterloggedNativeStreamClip.h"
#include "projection/core/ProjectionInternalTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include <glm/glm.hpp>

#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TessellatorQuadInfo.h"
#include "mc/deps/minecraft_renderer/renderer/MeshData.h"

namespace lholo::projection::detail {
namespace {

WaterloggedQuadPositions pointsForQuad(
    std::span<glm::vec3 const> positions
) {
    WaterloggedQuadPositions points{};
    for (std::size_t i = 0; i < 4; ++i)
        points[i] = {positions[i].x, positions[i].y, positions[i].z};
    return points;
}

// Interpolate both native UV channels without assuming fixed winding or a
// particular native texture scale. Only valid rectangular source quads reach
// this function; the four source corners are therefore a bijection.
glm::vec2 sampleQuadUv(
    std::span<glm::vec3 const>     vertices,
    std::span<glm::vec2 const>     uvs,
    WaterloggedFaceRect const&    original,
    std::array<float, 3> const&   newPosition
) {
    auto const fraction = waterloggedFraction(original, newPosition);
    auto const axisU = (original.axis + 1) % 3;
    auto const axisV = (original.axis + 2) % 3;
    glm::vec2 result{0.F, 0.F};
    for (std::size_t i = 0; i < 4; ++i) {
        auto const uHigh = std::fabs(vertices[i][axisU] - original.u1) <= WaterloggedEdgeEpsilon;
        auto const vHigh = std::fabs(vertices[i][axisV] - original.v1) <= WaterloggedEdgeEpsilon;
        auto const weight = (uHigh ? fraction[0] : 1.F - fraction[0])
            * (vHigh ? fraction[1] : 1.F - fraction[1]);
        result += uvs[i] * weight;
    }
    return result;
}

} // namespace

WaterloggedClipReport clipWaterloggedNativeStream(
    Tessellator& tessellator,
    std::vector<PraxisCompatLiquidKind>& kinds,
    std::vector<std::size_t>& owners,
    std::span<WaterloggedBodyCellCoverage const> coverage
) {
    WaterloggedClipReport report{};
    auto& data = tessellator.mMeshData.get();
    auto& positions = data.mPositions.get();
    auto& uv0 = data.mTextureUVs[0].get();
    auto& uv1 = data.mTextureUVs[1].get();
    auto& colors = data.mColors.get();
    auto& quadInfo = tessellator.mQuadInfoList.get();
    report.beforeVertices = positions.size();
    report.afterVertices = positions.size();
    if (coverage.empty()
        || std::none_of(coverage.begin(), coverage.end(), [](auto const& cell) {
            return !cell.solidFaces.empty();
        })
        || positions.empty() || positions.size() % 4U != 0U
        || data.mMode != mce::PrimitiveMode::QuadList
        || !data.mIndices.get().empty()
        || uv0.size() != positions.size()
        || (!uv1.empty() && uv1.size() != positions.size())
        || colors.size() != positions.size()
        || kinds.size() != positions.size() || owners.size() != positions.size()
        || (!quadInfo.empty() && quadInfo.size() != positions.size() / 4U)) {
        return report;
    }
    // Full typed native streams are preserved for non-target families.  R8
    // enables subdivision only for the verified water fixture stream layout:
    // positions/color/uv0/optional uv1/quad metadata. Otherwise skip.
    if (!data.mNormals.get().empty() || !data.mTangents.get().empty()
        || !data.mBoneId0s.get().empty() || !data.mTextureUVs[2].get().empty()
        || !data.mPBRTextureIndices.get().empty() || !data.mMERS.get().empty()
        || !data.mGeoType.get().empty()) {
        return report;
    }
    // R9 keeps the original per-cell ownership but avoids a linear lookup
    // for every liquid quad on dense multi-thousand-cell projections.
    std::vector<WaterloggedBodyCellCoverage const*> eligible;
    eligible.reserve(coverage.size());
    for (auto const& cell : coverage) {
        if (!cell.solidFaces.empty() && cell.family != WaterloggedClipFamily::None)
            eligible.push_back(&cell);
    }
    if (eligible.empty()) return report;
    std::sort(eligible.begin(), eligible.end(), [](auto const* a, auto const* b) {
        return a->structureIndex < b->structureIndex;
    });
    // No changed data is ever published until the entire replacement stream
    // is validated, including attribute alignment and nonempty sections.
    auto stagedPositions = positions;
    auto stagedUv0 = uv0;
    auto stagedUv1 = uv1;
    auto stagedColors = colors;
    auto stagedKinds = kinds;
    auto stagedOwners = owners;
    auto stagedQuadInfo = quadInfo;
    stagedPositions.clear();
    stagedUv0.clear();
    stagedUv1.clear();
    stagedColors.clear();
    stagedKinds.clear();
    stagedOwners.clear();
    stagedQuadInfo.clear();

    bool changed{};
    auto const quadCount = positions.size() / 4U;
    for (std::size_t quad = 0; quad < quadCount; ++quad) {
        auto const base = quad * 4U;
        std::span<glm::vec3 const> sourcePos{positions.data() + base, 4U};
        std::span<glm::vec2 const> sourceUv0{uv0.data() + base, 4U};
        std::span<glm::vec2 const> sourceUv1{
            uv1.empty() ? nullptr : uv1.data() + base,
            uv1.empty() ? 0U : 4U
        };
        auto const appendOriginal = [&] {
            for (std::size_t i = base; i < base + 4U; ++i) {
                stagedPositions.push_back(positions[i]);
                stagedUv0.push_back(uv0[i]);
                if (!uv1.empty()) stagedUv1.push_back(uv1[i]);
                stagedColors.push_back(colors[i]);
                stagedKinds.push_back(kinds[i]);
                stagedOwners.push_back(owners[i]);
            }
            if (!quadInfo.empty()) stagedQuadInfo.push_back(quadInfo[quad]);
        };

        bool homogeneous = true;
        for (std::size_t corner = 1; corner < 4U; ++corner) {
            homogeneous &= owners[base + corner] == owners[base]
                && kinds[base + corner] == kinds[base]
                && colors[base + corner] == colors[base];
        }
        if (!homogeneous || kinds[base] != PraxisCompatLiquidKind::Water) {
            appendOriginal();
            continue;
        }
        auto const it = std::lower_bound(
            eligible.begin(), eligible.end(), owners[base],
            [](WaterloggedBodyCellCoverage const* cell, std::size_t owner) {
                return cell->structureIndex < owner;
            }
        );
        if (it == eligible.end() || (*it)->structureIndex != owners[base]) {
            appendOriginal();
            continue;
        }
        auto const& cell = **it;
        auto const rawPoints = pointsForQuad(sourcePos);
        auto const sourceRect = waterloggedAxisRect(rawPoints);
        auto const fragments = clipWaterloggedNativeFace(rawPoints, cell.solidFaces);
        if (!sourceRect || !fragments) {
            appendOriginal();
            continue;
        }
        changed = true;
        ++report.clippedSourceQuads;
        switch (cell.family) {
        case WaterloggedClipFamily::Stairs: ++report.stairClippedQuads; break;
        case WaterloggedClipFamily::Slab: ++report.slabClippedQuads; break;
        case WaterloggedClipFamily::Trapdoor: ++report.trapdoorClippedQuads; break;
        case WaterloggedClipFamily::None: break;
        }
        if (fragments->empty()) {
            ++report.removedSourceQuads;
            continue;
        }
        report.addedFragmentQuads += fragments->size();
        for (auto const& fragment : *fragments) {
            glm::vec3 centroid{0.F};
            for (std::size_t corner = 0; corner < 4U; ++corner) {
                auto const out = waterloggedCorner(*sourceRect, fragment, rawPoints[corner]);
                glm::vec3 pos{out[0], out[1], out[2]};
                centroid += pos;
                stagedPositions.push_back(pos);
                stagedUv0.push_back(sampleQuadUv(sourcePos, sourceUv0, *sourceRect, out));
                if (!uv1.empty())
                    stagedUv1.push_back(sampleQuadUv(sourcePos, sourceUv1, *sourceRect, out));
                stagedColors.push_back(colors[base]);
                stagedKinds.push_back(kinds[base]);
                stagedOwners.push_back(owners[base]);
            }
            if (!quadInfo.empty()) {
                auto entry = quadInfo[quad];
                entry.centroid.get() = Vec3{centroid.x * .25F,
                                             centroid.y * .25F,
                                             centroid.z * .25F};
                stagedQuadInfo.push_back(entry);
            }
        }
    }
    if (!changed || stagedPositions.empty()
        || stagedPositions.size() > positions.size() * 4U
        || stagedPositions.size() != stagedUv0.size()
        || stagedPositions.size() != stagedColors.size()
        || stagedPositions.size() != stagedKinds.size()
        || stagedPositions.size() != stagedOwners.size()
        || (!uv1.empty() && stagedPositions.size() != stagedUv1.size())
        || (!quadInfo.empty() && stagedQuadInfo.size() != stagedPositions.size() / 4U)
        || stagedPositions.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        return report;
    }
    for (std::size_t i = 0; i < stagedPositions.size(); ++i) {
        auto const& p = stagedPositions[i];
        auto const& uv = stagedUv0[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
            || !std::isfinite(uv.x) || !std::isfinite(uv.y)) return report;
    }
    positions.swap(stagedPositions);
    uv0.swap(stagedUv0);
    uv1.swap(stagedUv1);
    colors.swap(stagedColors);
    kinds.swap(stagedKinds);
    owners.swap(stagedOwners);
    quadInfo.swap(stagedQuadInfo);
    auto minPos = positions.front(), maxPos = positions.front();
    auto minUv = uv0.front(), maxUv = uv0.front();
    for (std::size_t i = 1; i < positions.size(); ++i) {
        minPos = glm::min(minPos, positions[i]);
        maxPos = glm::max(maxPos, positions[i]);
        minUv = glm::min(minUv, uv0[i]);
        maxUv = glm::max(maxUv, uv0[i]);
    }
    data.mAABB.get() = {minPos, maxPos};
    data.mUVAABB.get() = {minUv, maxUv};
    tessellator.mCount = static_cast<std::uint32_t>(positions.size());
    tessellator.mMaxVertexCount = std::max(
        tessellator.mMaxVertexCount,
        static_cast<std::uint32_t>(positions.size())
    );
    report.afterVertices = positions.size();
    report.committed = true;
    return report;
}
} // namespace lholo::projection::detail
