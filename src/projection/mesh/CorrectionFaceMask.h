#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace lholo::projection::detail {
// Face order matches native correction fill: -Z,+Z,-X,+X,-Y,+Y.
// A face is kept only if its priority exceeds the neighbor's priority.
template<class NeighborPriority>
std::uint8_t correctionExposedFaceMask(int priority, NeighborPriority&& neighbor) {
    constexpr std::array<std::array<int, 3>,6> offsets{{
        {{0,0,-1}}, {{0,0,1}}, {{-1,0,0}}, {{1,0,0}},
        {{0,-1,0}}, {{0,1,0}}
    }};
    std::uint8_t mask{};
    for(std::size_t i=0;i<offsets.size();++i) {
        auto const& d=offsets[i];
        if(priority>neighbor(d[0],d[1],d[2]))
            mask |= static_cast<std::uint8_t>(1U<<i);
    }
    return mask;
}
}
