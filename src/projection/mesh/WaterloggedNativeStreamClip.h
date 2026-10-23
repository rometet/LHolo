// LHolo - R8 conservative waterlogged stair/liquid face clipping.
// Only the native Exact Replay section build calls this helper. It never
// accesses the live world, graphics context or shared GPU resources.
#pragma once

#include "projection/core/WaterloggedFaceClip.h"
#include "projection/core/WaterloggedClipFamily.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class Tessellator;

namespace lholo::projection::detail {
enum class PraxisCompatLiquidKind : std::uint8_t;

struct WaterloggedBodyCellCoverage {
    std::size_t structureIndex{};
    std::vector<WaterloggedFaceRect> solidFaces;
    WaterloggedClipFamily family{WaterloggedClipFamily::None};
    std::vector<WaterloggedFaceRect> cutoutFaces; // never cull: inset water only
    std::array<float, 3> cellCenter{};
};
struct WaterloggedClipReport {
    std::size_t beforeVertices{};
    std::size_t afterVertices{};
    std::size_t clippedSourceQuads{};
    std::size_t removedSourceQuads{};
    std::size_t addedFragmentQuads{};
    std::size_t stairClippedQuads{};
    std::size_t slabClippedQuads{};
    std::size_t trapdoorClippedQuads{};
    std::size_t cutoutInsetWaterQuads{};
    bool committed{};
};

// Keeps original native MeshData untouched unless all typed stream invariants
// hold and at least one reliable axis-aligned same-cell overlap is found.
// Call after native same-liquid culling, before origin conversion/derivation.
[[nodiscard]] WaterloggedClipReport clipWaterloggedNativeStream(
    Tessellator& tessellator,
    std::vector<PraxisCompatLiquidKind>& kinds,
    std::vector<std::size_t>& owners,
    std::span<WaterloggedBodyCellCoverage const> coverage
);

} // namespace lholo::projection::detail
