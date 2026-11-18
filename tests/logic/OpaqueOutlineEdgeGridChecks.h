#pragma once
#include "projection/mesh/OpaqueOutlineEdgeGrid.h"
#include <cstdint>
#include <random>
#include <set>
#include <tuple>
namespace lholo::tests {
template<class Check> void runOpaqueOutlineEdgeGridChecks(Check check) {
    using lholo::projection::detail::OpaqueOutlineEdgeGrid;
    OpaqueOutlineEdgeGrid grid;
    check(grid.reset(0,0,0,1,0,0));
    check(grid.bytes()>0);
    int kept=0;
    auto edge=[&](int axis,int x,int y,int z,int color) {
        if(grid.claim(axis,x,y,z,static_cast<std::uint8_t>(color))) ++kept;
    };
    for(int x=0;x<2;++x) {
        for(int y=0;y<=1;++y) for(int z=0;z<=1;++z) edge(0,x,y,z,1);
        for(int x2=x;x2<=x+1;++x2) for(int z=0;z<=1;++z) edge(1,x2,0,z,1);
        for(int x2=x;x2<=x+1;++x2) for(int y=0;y<=1;++y) edge(2,x2,y,0,1);
    }
    check(kept==20);
    check(!grid.claim(0,0,0,0,1));
    check(grid.claim(0,0,0,0,2));
    check(!grid.claim(0,0,0,0,2));
    check(grid.claim(0,0,0,0,4));
    check(grid.claim(0,0,0,0,8));
    check(grid.claim(0,15,0,0,1));
    check(!grid.reset(0,0,0,1000,0,0));
    check(grid.bytes()==0);
    check(grid.claim(0,0,0,0,1));
    check(grid.reset(-5,-4,-3,-4,-3,-2));
    check(grid.claim(2,-5,-4,-3,1));
    check(!grid.claim(2,-5,-4,-3,1));
    check(grid.claim(1,-5,-4,-3,1));
    check(!grid.reset(17000000,0,0,17000001,0,0));
    check(grid.reset(0,0,0,15,15,15));
    std::size_t claimed{}, expanded{};
    for(int axis=0;axis<3;++axis) {
        for(int x=0;x<=16;++x) for(int y=0;y<=16;++y)
            for(int z=0;z<=16;++z) {
                if ((axis==0 && x==16) || (axis==1 && y==16)
                    || (axis==2 && z==16)) continue;
                if(grid.claim(axis,x,y,z,1)) ++claimed;
            }
    }
    auto const runs=grid.forEachMergedRun(1,[&](
        int axis,std::int64_t x,std::int64_t y,std::int64_t z,std::size_t length) {
        check(axis>=0 && axis<3);
        check(length==16);
        check(x>=0 && x<=16 && y>=0 && y<=16 && z>=0 && z<=16);
        expanded+=length;
    });
    check(claimed==13872);
    check(runs==867);
    check(expanded==claimed);
    check(grid.forEachMergedRun(2,[](auto...) {})==0);
    check(grid.claim(0,0,0,0,2));
    check(grid.forEachMergedRun(2,[](auto...) {})==1);
    check(grid.reset(-4,-4,-4,3,3,3));
    using Edge=std::tuple<int,int,int,int>;
    std::set<Edge> original, restored;
    std::mt19937 random{0xFA1282U};
    for(int i=0;i<1000;++i) {
        int axis=static_cast<int>(random()%3U);
        int x=static_cast<int>(random()%8U)-4;
        int y=static_cast<int>(random()%8U)-4;
        int z=static_cast<int>(random()%8U)-4;
        auto const edgeKey=Edge{axis,x,y,z};
        original.insert(edgeKey);
        grid.claim(axis,x,y,z,1);
    }
    auto const runCount=grid.forEachMergedRun(1,[&](
        int axis,std::int64_t x,std::int64_t y,std::int64_t z,std::size_t len) {
        for(std::size_t i=0;i<len;++i) {
            auto const key=Edge{axis,static_cast<int>(x)+(axis==0?static_cast<int>(i):0),
                static_cast<int>(y)+(axis==1?static_cast<int>(i):0),
                static_cast<int>(z)+(axis==2?static_cast<int>(i):0)};
            check(restored.insert(key).second);
        }
    });
    check(runCount<=original.size());
    check(restored==original);
}
}
