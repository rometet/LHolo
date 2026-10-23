// LHolo: static client-side bubble-column schematic glyph geometry.
// Not a native particle effect and never changes the real Minecraft world.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace lholo::projection::detail {
using BubbleGlyphVertex = std::array<float, 3>;
using BubbleGlyphQuad = std::array<BubbleGlyphVertex, 4>;
inline constexpr std::size_t BubbleGlyphRingCount = 2;
inline constexpr std::size_t BubbleGlyphSegments = 6;
inline constexpr std::size_t BubbleGlyphPlanes = 2;
inline constexpr std::size_t BubbleGlyphSides = 2;
inline constexpr std::size_t BubbleGlyphQuadsPerCell =
    BubbleGlyphRingCount * BubbleGlyphSegments * BubbleGlyphPlanes * BubbleGlyphSides;
inline constexpr std::size_t BubbleGlyphVerticesPerCell = BubbleGlyphQuadsPerCell * 4U;

// Emit crossed hollow bubble rings with fixed world-space coordinates.
// Each ring stays within its source cell. Low-opacity tinted white concrete
// texels are chosen by the calling renderer, not this pure geometry helper.
template <class Emit>
void emitBubbleColumnGlyph(
    std::array<float,3> const& blockOrigin,
    bool dragDown,
    Emit&& emit
) {
    constexpr float tau = 6.28318530717958647692F;
    constexpr float outerRadius = .105F;
    constexpr float innerRadius = .079F;
    for (std::size_t bubble = 0; bubble < BubbleGlyphRingCount; ++bubble) {
        auto const cx = blockOrigin[0] + (bubble == 0U ? .38F : .64F);
        auto const cy = blockOrigin[1]
            + (dragDown ? (bubble == 0U ? .72F : .32F)
                        : (bubble == 0U ? .28F : .68F));
        auto const cz = blockOrigin[2] + (bubble == 0U ? .54F : .41F);
        for (std::size_t plane = 0; plane < BubbleGlyphPlanes; ++plane) {
            auto point = [&](float rad, float angle) {
                auto const a = angle * tau;
                auto const dx = std::cos(a) * rad;
                auto const dy = std::sin(a) * rad;
                return plane == 0U
                    ? BubbleGlyphVertex{cx + dx,cy + dy,cz}
                    : BubbleGlyphVertex{cx,cy + dy,cz + dx};
            };
            for (std::size_t seg = 0; seg < BubbleGlyphSegments; ++seg) {
                float const from = static_cast<float>(seg)
                    / static_cast<float>(BubbleGlyphSegments);
                float const to = static_cast<float>(seg + 1U)
                    / static_cast<float>(BubbleGlyphSegments);
                BubbleGlyphQuad const face{
                    point(outerRadius,from),
                    point(outerRadius,to),
                    point(innerRadius,to),
                    point(innerRadius,from)
                };
                emit(face);
                emit(BubbleGlyphQuad{face[3],face[2],face[1],face[0]});
            }
        }
    }
}
} // namespace lholo::projection::detail
