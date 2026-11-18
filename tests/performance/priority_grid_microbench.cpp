#include "projection/mesh/LocalCorrectionPriorityGrid.h"
#include "projection/core/ProjectionCoordinateKey.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <unordered_map>

int main() {
    using namespace lholo::projection::detail;
    using Clock=std::chrono::steady_clock;
    std::unordered_map<SubChunkKey,std::uint8_t,SubChunkKeyHash> virtualWorld;
    virtualWorld.reserve(260000);
    std::uint32_t seed=42;
    for(int i=0;i<245000;++i) {
        seed=seed*1664525U+1013904223U;
        virtualWorld.try_emplace({1000+i%121, i/121%118, 1000+i/(121*118)},
            static_cast<std::uint8_t>(seed%3));
    }
    std::uint8_t expected[18][18][18]{};
    for(int x=0;x<18;++x)
        for(int y=0;y<18;++y)
            for(int z=0;z<18;++z) {
                seed=seed*1664525U+1013904223U;
                expected[x][y][z]=static_cast<std::uint8_t>((seed>>28)%3);
                virtualWorld[{x,y,z}]=expected[x][y][z];
            }
    constexpr int repeats=64;
    std::uint64_t first{};
    auto began=Clock::now();
    for(int run=0;run<repeats;++run)
        for(int x=1;x<=16;++x)
            for(int y=1;y<=16;++y)
                for(int z=1;z<=16;++z) {
                    for(auto const [dx,dy,dz]:{SubChunkKey{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}}) {
                        auto it=virtualWorld.find({x+dx,y+dy,z+dz});
                        first+=it==virtualWorld.end()?0:it->second;
                    }
                }
    auto const fullEnd=Clock::now();
    std::uint64_t second{};
    auto const localStart=Clock::now();
    for(int run=0;run<repeats;++run) {
        LocalCorrectionPriorityGrid grid;
        if(!grid.reset(0,0,0,17,17,17))return 2;
        for(int x=0;x<18;++x)
            for(int y=0;y<18;++y)
                for(int z=0;z<18;++z)
                    grid.add(x,y,z,expected[x][y][z]);
        for(int x=1;x<=16;++x)
            for(int y=1;y<=16;++y)
                for(int z=1;z<=16;++z) {
                    second+=grid.get(x-1,y,z);
                    second+=grid.get(x+1,y,z);
                    second+=grid.get(x,y-1,z);
                    second+=grid.get(x,y+1,z);
                    second+=grid.get(x,y,z-1);
                    second+=grid.get(x,y,z+1);
                }
    }
    auto const end=Clock::now();
    auto ms=[](Clock::time_point a,Clock::time_point b){
        return std::chrono::duration<double,std::milli>(b-a).count();
    };
    std::printf("virtual_world_entries=%zu repeats=%d old_hash_ms=%.3f local_grid_including_build_ms=%.3f grid_bytes=5832 checksum_equal=%d\n",
        virtualWorld.size(),repeats,ms(began,fullEnd),ms(localStart,end),first==second);
    return first==second?0:1;
}