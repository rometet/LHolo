#include "projection/core/LiquidBoundaryMaskCache.h"
inline void boundaryCommandChecks() {
    for(float plane:{-16.F,0.F,16.F}) {
        std::vector<Vec3> vertices{{plane,0,0},{plane,1,0},{plane,1,1},{plane,0,1},
                                   {plane,0,1},{plane,1,1},{plane,1,0},{plane,0,0},
                                   {plane+1,0,0},{plane+1,1,0},{plane+1,1,1},{plane+1,0,1}};
        std::vector<std::uint8_t> water(vertices.size(),0);
        auto mask=buildNativeLiquidInternalFaceCullMask<Vec3>(vertices,NativeLiquidFullFaceTolerance,
                                                            std::span<std::uint8_t const>{water});
        CHECK(mask.valid && mask.facePairs==1 && mask.removedVertices()==8);
        LiquidBoundaryMaskCache cache;
        std::array<LiquidSectionQuadCount,2> initial{{{0,1},{1,2}}}, reversed{{{1,2},{0,1}}};
        CHECK(cache.remember(initial,mask));
        auto permuted=cache.forOrder(reversed);
        CHECK(permuted && permuted->removeQuads==std::vector<std::uint8_t>({1,0,1}));
        // Every canonical/derived per-vertex stream receives the same mask;
        // distinct payload tags make a UV/color/normal misalignment visible.
        for(unsigned stream=0;stream<13;++stream) {
            std::vector<unsigned> values;
            for(unsigned i=0;i<12;++i)values.push_back(stream*100+i);
            CHECK(compactLiquidQuadField(values,mask.removeQuads,4));
            CHECK(values==std::vector<unsigned>({stream*100+8,stream*100+9,stream*100+10,stream*100+11}));
        }
        std::vector<unsigned> quadInfo{100,200,300};
        CHECK(compactLiquidQuadField(quadInfo,mask.removeQuads,1));
        CHECK(quadInfo==std::vector<unsigned>({300}));
        std::vector<unsigned> unsupported{1,2};
        CHECK(!compactLiquidQuadField(unsupported,mask.removeQuads,4));
        CHECK(unsupported==std::vector<unsigned>({1,2}));
        water[4]=water[5]=water[6]=water[7]=1;
        auto mixed=buildNativeLiquidInternalFaceCullMask<Vec3>(vertices,NativeLiquidFullFaceTolerance,
                                                             std::span<std::uint8_t const>{water});
        CHECK(mixed.valid && mixed.facePairs==0);
        for(auto& p:vertices)p.y*=.5F;
        auto partial=buildNativeLiquidInternalFaceCullMask<Vec3>(vertices);
        CHECK(partial.valid && partial.facePairs==0); // conservative partial-face policy retained
        cache.clear();CHECK(!cache.forOrder(initial));
    }
}
