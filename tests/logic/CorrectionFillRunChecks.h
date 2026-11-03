#pragma once

#include "projection/mesh/CorrectionFillRunGrid.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace lholo::tests {
template<class Check>
void runCorrectionFillRunChecks(Check check) {
    using lholo::projection::detail::CorrectionFillRunGrid;
    CorrectionFillRunGrid grid;
    check(!grid.reset(0,0,0,16,15,15)); // 17x16x16 must fail open
    check(!grid.reset(8,0,0,7,0,0));   // inverted range
    // Diverse densities and translations exercise the same cell/face masks
    // as the original per-face Tessellator path.
    std::mt19937 rng{0xF17A6A1u};
    for (int scenario = 0; scenario < 180; ++scenario) {
        int const sx = 1 + static_cast<int>(rng() % 16);
        int const sy = 1 + static_cast<int>(rng() % 16);
        int const sz = 1 + static_cast<int>(rng() % 16);
        int const ox = scenario % 2 == 0 ? -10000 : 10000;
        int const oy = scenario % 3 == 0 ? -100 : 100;
        int const oz = scenario % 5 == 0 ? -24000 : 24000;
        check(grid.reset(ox,oy,oz,ox+sx-1,oy+sy-1,oz+sz-1));
        auto const index = [&](int x,int y,int z) {
            return (static_cast<std::size_t>(x)*sy + static_cast<std::size_t>(y))*sz
                + static_cast<std::size_t>(z);
        };
        std::vector<std::uint8_t> expected(static_cast<std::size_t>(sx*sy*sz));
        std::vector<std::uint8_t> result(expected.size());
        std::size_t faces{};
        for (int x=0;x<sx;++x)
            for (int y=0;y<sy;++y)
                for (int z=0;z<sz;++z) {
                    // Full slabs, hollow shells, sparse random faces, and dense
                    // volumetric cells whose only faces are the outer shell.
                    std::uint8_t mask = 0;
                    if (scenario % 4 == 0) mask = 0b00111111;
                    else if (scenario % 4 == 1) {
                        if (z == 0) mask |= 1;
                        if (z == sz-1) mask |= 2;
                        if (x == 0) mask |= 4;
                        if (x == sx-1) mask |= 8;
                        if (y == 0) mask |= 16;
                        if (y == sy-1) mask |= 32;
                    } else if (scenario % 4 == 2) mask = static_cast<std::uint8_t>(rng() & 63);
                    else if ((x+y+z)%3 == 0) mask = 63;
                    expected[index(x,y,z)] = mask;
                    grid.add(ox+x,oy+y,oz+z,mask);
                    for (int side=0;side<6;++side)
                        if(mask & (1u<<side)) ++faces;
                }
        std::size_t merged{}, recoveredFaces{};
        grid.forEachRun([&](int side,int x,int y,int z,int length) {
            check(side>=0 && side<6);
            check(length>0 && length<=16);
            ++merged;
            for (int offset=0;offset<length;++offset) {
                int const rx = x-ox+((side==2 || side==3)?0:offset);
                int const ry = y-oy;
                int const rz = z-oz+((side==2 || side==3)?offset:0);
                check(rx>=0 && rx<sx && ry>=0 && ry<sy && rz>=0 && rz<sz);
                if(rx>=0 && rx<sx && ry>=0 && ry<sy && rz>=0 && rz<sz) {
                    auto& mask = result[index(rx,ry,rz)];
                    check((mask & (1u<<side))==0); // no duplicate face
                    mask |= static_cast<std::uint8_t>(1u<<side);
                    ++recoveredFaces;
                }
            }
        });
        check(expected == result); // every original directional face preserved
        check(faces == recoveredFaces);
        check(merged <= faces);
        std::vector<std::uint8_t> fromRectangles(expected.size());
        std::size_t rectQuads{}, rectUnitFaces{};
        grid.forEachRectangle([&](int side,int x,int y,int z,int width,int height) {
            ++rectQuads;
            check(side>=0 && side<6);
            check(width>0 && width<=16);
            check(height>0 && height<=16);
            for (int dv=0;dv<height;++dv)
                for (int du=0;du<width;++du) {
                    int const rx=x-ox+((side==2||side==3)?0:du);
                    int const ry=y-oy+((side==4||side==5)?0:dv);
                    int const rz=z-oz+((side==2||side==3)?du
                        :((side==4||side==5)?dv:0));
                    check(rx>=0 && rx<sx && ry>=0 && ry<sy && rz>=0 && rz<sz);
                    if(rx>=0 && rx<sx && ry>=0 && ry<sy && rz>=0 && rz<sz) {
                        auto& mask=fromRectangles[index(rx,ry,rz)];
                        check((mask & (1u<<side))==0); // no double-painted face
                        mask |= static_cast<std::uint8_t>(1u<<side);
                        ++rectUnitFaces;
                    }
                }
        });
        check(fromRectangles==expected);
        check(rectUnitFaces==faces);
        check(rectQuads<=faces);
        if (scenario % 4 == 0 && sx == 16 && sy == 16 && sz == 16)
            check(merged < faces/4);
    }
    // Full dense 16^3 correction shell: 1,536 exposed unit faces collapse
    // to 96 co-planar strips, with exactly identical logical surface cover.
    check(grid.reset(-16,64,8000,-1,79,8015));
    for (int x=0;x<16;++x)
        for (int y=0;y<16;++y)
            for (int z=0;z<16;++z) {
                std::uint8_t mask{};
                if(z==0) mask|=1;
                if(z==15) mask|=2;
                if(x==0) mask|=4;
                if(x==15) mask|=8;
                if(y==0) mask|=16;
                if(y==15) mask|=32;
                grid.add(x-16,y+64,z+8000,mask);
            }
    std::size_t shellRuns{}, shellFaces{};
    grid.forEachRun([&](int,int,int,int,int length) {
        ++shellRuns;
        shellFaces+=static_cast<std::size_t>(length);
    });
    check(shellFaces==1536);
    check(shellRuns==96);
    std::size_t shellRectangles{}, shellRectangleFaces{};
    grid.forEachRectangle([&](int,int,int,int,int width,int height) {
        ++shellRectangles;
        shellRectangleFaces+=static_cast<std::size_t>(width*height);
    });
    check(shellRectangles==6);
    check(shellRectangleFaces==1536);
}
} // namespace lholo::tests
