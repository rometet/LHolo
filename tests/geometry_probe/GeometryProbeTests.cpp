#include "projection/mesh/NativeGeometryProbe.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
#include "structure/formats/BedrockNbtScanner.h"
using namespace lholo::projection::detail;
struct Pos { float x{},y{},z{}; bool operator==(Pos const&) const=default; };
struct Uv { float x{},y{}; bool operator==(Uv const&) const=default; };
int main(int argc, char** argv) {
    NativeGeometryProbe probe;probe.active=true;probe.batch=1;probe.generation=9;
    for(std::size_t i=0;i<NativeGeometryProbe::CellLimit;++i)assert(probe.select(i));
    assert(!probe.select(100));assert(probe.select(0));
    assert(!gActiveNativeGeometryProbe);
    { ScopedNativeGeometryProbe outer(probe);assert(gActiveNativeGeometryProbe==&probe);
        NativeGeometryProbe inner;
        try { ScopedNativeGeometryProbe nested(inner);assert(gActiveNativeGeometryProbe==&inner);throw 1; }
        catch(int) { assert(gActiveNativeGeometryProbe==&probe); }
    }
    assert(!gActiveNativeGeometryProbe);
    std::vector<Pos> positions{{10,2,10},{11,2,10},{11,2,11},{10,2,11}};
    std::vector<Uv> uvs{{.1f,.2f},{.2f,.2f},{.2f,.3f},{.1f,.3f}};
    std::vector<unsigned> colors(4,0xA04080C0);
    auto const originalPositions=positions;auto const originalUvs=uvs;auto const originalColors=colors;
    auto emit=[](std::string const& r){std::cout<<"PRAXIS_GEOMETRY_CAPTURE "<<r<<'\n';};
    probe.dump("body","native",0,"fixture:slab","minecraft:water",5,1,false,false,true,0,positions,uvs,colors,0,0,0,0,0,0,emit);
    probe.dump("liquid","atlas",0,"fixture:slab","minecraft:water",3,1,false,false,true,0,positions,uvs,colors,0,0,0,0,0,0,emit);
    for(auto& p:positions) { p.x-=10;p.z-=10; }
    probe.dump("liquid","final",0,"section","section",3,1,false,false,true,0,positions,uvs,colors,0,0,0,10,0,10,emit);
    positions=originalPositions;
    probe.dump("body","native",1,"fixture:dry","none",5,1,false,false,true,0,positions,uvs,colors,0,0,0,0,0,0,emit);
    assert(positions==originalPositions && uvs==originalUvs && colors==originalColors);
    positions.resize(300);uvs.resize(300);colors.resize(300);
    probe.dump("body","native",2,"fixture:bounded","water",5,1,false,false,true,0,positions,uvs,colors,0,0,0,0,0,0,emit);
    uvs.pop_back();
    probe.dump("body","native",3,"fixture:invalid","water",5,1,false,false,true,0,positions,uvs,colors,0,0,0,0,0,0,emit);
    // Throwing observation callbacks must preserve every input and not escape.
    probe.dump("body","native",4,"fixture:throw","water",5,1,false,false,true,0,positions,uvs,colors,0,0,0,0,0,0,[](std::string const&){throw 1;});
    positions[0].x=std::numeric_limits<float>::quiet_NaN();
    probe.dump("body","native",5,"fixture:nonfinite","water",5,1,false,false,true,0,positions,uvs,colors,0,0,0,0,0,0,emit);
    for(int i=1;i<argc;++i) {
        std::ifstream file(argv[i],std::ios::binary);assert(file.good());
        std::string bytes(std::istreambuf_iterator<char>{file},{});
        lholo::structure::detail::BedrockNbtScanner scanner{bytes};assert(scanner.scan());
        std::cout<<"Exact captured native palette fixture NBT scan PASS "<<argv[i]<<'\n';
    }
    std::cout<<"Geometry capture CPU ownership/bounds/scope PASS; synthetic native inputs; game NOT_RUN\n";
}
