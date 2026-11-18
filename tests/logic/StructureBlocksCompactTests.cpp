#include "structure/StructureBlocks.h"
#include <cstdio>
#include <cstdint>

int runStructureBlocksCompactTests() {
    using lholo::structure::StructureBlocks;
    using lholo::structure::StructureBlock;
    int fails{};
    auto check=[&](bool ok,char const* label) {
        if(!ok) {++fails;std::fprintf(stderr,"CompactStructure: %s\n",label);}
    };
    StructureBlocks cells;
    constexpr int sx=128,sy=64,sz=32;
    constexpr std::size_t total=static_cast<std::size_t>(sx)*sy*sz;
    cells.reserve(total);
    for(int x=0;x<sx;++x) for(int y=0;y<sy;++y) for(int z=0;z<sz;++z) {
        StructureBlock value{};
        value.x=x;value.y=y;value.z=z;value.materialIndex=z%4;
        cells.push_back(std::move(value));
    }
    auto const originalBytes=cells.storageBytes();
    check(originalBytes>=total*sizeof(StructureBlock),"prepacked size");
    check(cells.packFromValues(sx,sy,sz),"packed large sorted input");
    check(cells.isCompact(),"packed state");
    auto const compressedBytes=cells.storageBytes();
    check(compressedBytes<originalBytes/3,"storage capacity drops by more than 3x");
    check(cells.size()==total,"cell count unchanged");
    std::uint64_t allCounts{};
    cells.forEachCountedValue([&](StructureBlock const& cell,std::uint64_t count) {
        check(cell.materialIndex>=0 && cell.materialIndex<4,"palette material");
        allCounts+=count;
    });
    check(allCounts==total,"material counts preserved");
    for (auto x: {0,17,63,127}) for(auto y: {0,23,63}) for(auto z: {0,7,31}) {
        auto const index=(static_cast<std::size_t>(x)*sy+y)*sz+z;
        auto const v=cells[index];
        check(v.x==x&&v.y==y&&v.z==z&&v.materialIndex==z%4,"indexed cell roundtrip");
        auto const found=cells.find(x,y,z);
        check(found.has_value()&&*found==index,"coordinate lookup");
    }
    check(!cells.find(128,0,0),"bounds X");
    check(!cells.find(0,64,0),"bounds Y");
    check(!cells.find(0,0,32),"bounds Z");
    check(!cells.find(-1,0,0),"negative bounds");
    auto copy=cells;
    auto& expanded=copy.mutableValues();
    check(!copy.isCompact()&&cells.isCompact(),"copy mutates without changing source");
    check(expanded.size()==total && expanded[total-1].z==31,"expanded source fidelity");
    StructureBlocks malformed;
    malformed.push_back({0,0,0});
    malformed.push_back({0,0,0});
    check(!malformed.packFromValues(1,1,1,1),"duplicate coords fail closed");
    check(malformed.size()==2 && !malformed.isCompact(),"duplicate input retained");
    StructureBlocks tooSmall;
    tooSmall.push_back({0,0,0});
    check(!tooSmall.packFromValues(1,1,1),"tiny structure stays simple");
    std::printf("StructureBlocksCompactTests: original=%zu bytes packed=%zu bytes reduction=%.1f%% failures=%d\n",
        originalBytes,compressedBytes,100.0*(1.0-static_cast<double>(compressedBytes)/originalBytes),fails);
    return fails;
}