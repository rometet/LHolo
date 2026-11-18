// Select the twelve axis-aligned box edges that do not lie on the enclosing
// placement's outer wireframe. Input coordinates are *boundary* coordinates,
// i.e. the inclusive max block coordinate plus one.
#pragma once
#include <array>
#include <cstdint>

namespace lholo::render {
inline constexpr std::uint16_t kAllBoxWireframeEdges=0x0FFFu;
using BoxBoundary=std::array<std::int64_t,3>;

inline std::uint16_t boxEdgesExcludingOuter(
    BoxBoundary const& min,BoxBoundary const& max,
    BoxBoundary const& outerMin,BoxBoundary const& outerMax) noexcept {
    auto const onBoundary=[&](int axis,std::int64_t fixed){
        return fixed==outerMin[axis] || fixed==outerMax[axis];
    };
    std::uint16_t mask=kAllBoxWireframeEdges;
    // BoxWireframeMesh vertex order: four X edges, four Y edges, four Z
    // edges (interleaved for X/Y). A yellow edge is removed only when both
    // fixed-axis coordinates place it on the *blue outer edge*. Partial
    // collinear overlaps are therefore removed without splitting segments.
    for(int edge=0;edge<12;++edge) {
        bool coincident{};
        if(edge<8 && (edge%2)==0) {
            auto const y=(edge==0||edge==4)?min[1]:max[1];
            auto const z=edge<4?min[2]:max[2];
            coincident=onBoundary(1,y)&&onBoundary(2,z);
        }else if(edge<8) {
            auto const x=(edge==1||edge==5)?max[0]:min[0];
            auto const z=edge<4?min[2]:max[2];
            coincident=onBoundary(0,x)&&onBoundary(2,z);
        }else {
            auto const x=(edge==9||edge==10)?max[0]:min[0];
            auto const y=(edge==10||edge==11)?max[1]:min[1];
            coincident=onBoundary(0,x)&&onBoundary(1,y);
        }
        if(coincident)mask=static_cast<std::uint16_t>(mask&~(1u<<edge));
    }
    return mask;
}
} // namespace lholo::render
