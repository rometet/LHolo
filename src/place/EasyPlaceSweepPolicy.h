// LHolo: bounded, aimed floor sweep for Easy Place.
// A downward/upward ray chooses an XZ patch; horizontal rays choose a wall.
#pragma once
#include <array>
#include <cstdint>
#include <limits>
namespace lholo::place::detail {
using FloorOffset = std::array<int,2>;
[[nodiscard]] constexpr std::array<FloorOffset,4> easyPlaceFloorOffsets(
    float viewX,float viewZ
) noexcept {
    int const sx=viewX<0.0f?-1:1;
    int const sz=viewZ<0.0f?-1:1;
    return {{{0,0},{sx,0},{0,sz},{sx,sz}}};
}
[[nodiscard]] constexpr bool easyPlaceFloorFace(unsigned char face) noexcept {
    return face==0 || face==1;
}
// Avoid UB on schematic anchors at WORLD coordinate extrema.
[[nodiscard]] constexpr bool easyPlaceOffsetFits(int at,int offset) noexcept {
    std::int64_t const v=static_cast<std::int64_t>(at)+offset;
    return v>=std::numeric_limits<int>::min()
        && v<=std::numeric_limits<int>::max();
}
} // namespace lholo::place::detail
