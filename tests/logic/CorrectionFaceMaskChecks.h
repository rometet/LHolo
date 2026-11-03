#pragma once
#include "projection/mesh/CorrectionFaceMask.h"
#include <array>
#include <bit>
#include <cstdint>
#include <random>

namespace lholo::tests {
template<class Check> void runCorrectionFaceMaskChecks(Check check) {
    using lholo::projection::detail::correctionExposedFaceMask;
    constexpr std::array<std::array<int,3>,6> dirs{{
        {{0,0,-1}},{{0,0,1}},{{-1,0,0}},{{1,0,0}},
        {{0,-1,0}},{{0,1,0}}
    }};
    auto test=[&](int p, std::array<int,6> const& neighbors) {
        int calls{};
        auto actual=correctionExposedFaceMask(p,[&](int dx,int dy,int dz) {
            auto const d=dirs[static_cast<std::size_t>(calls++)];
            check(dx==d[0] && dy==d[1] && dz==d[2]);
            return neighbors[static_cast<std::size_t>(calls-1)];
        });
        std::uint8_t expected{};
        for(std::size_t i=0;i<6;++i)
            if(p>neighbors[i]) expected|=static_cast<std::uint8_t>(1U<<i);
        check(calls==6);
        check(actual==expected);
        check(std::popcount(static_cast<unsigned>(actual))<=6);
    };
    test(0,{0,0,0,0,0,0});
    test(1,{0,0,0,0,0,0});
    test(1,{1,1,1,1,1,1});
    test(3,{4,0,3,1,2,0});
    test(2,{1,3,2,0,4,2});
    std::mt19937 gen{0x1234};
    for(int i=0;i<2000;++i) {
        std::array<int,6> values{};
        for(auto& v:values) v=static_cast<int>(gen()%5);
        test(static_cast<int>(gen()%5),values);
    }
}
}
