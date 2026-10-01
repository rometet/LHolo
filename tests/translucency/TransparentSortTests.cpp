#include "projection/mesh/TransparentQuadSort.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <set>
#include <string>

using namespace lholo::projection::detail;
namespace {
std::size_t checks{};
#define CHECK(c) do { ++checks; if (!(c)) { std::cerr << "FAIL " << __LINE__ << ": " << #c << '\n'; std::exit(1); } } while (0)
std::vector<glm::vec3> quads(std::initializer_list<float> zs) {
    std::vector<glm::vec3> vertices;
    for (float z : zs) {
        vertices.insert(vertices.end(), {{0,0,z},{1,0,z},{1,1,z},{0,1,z}});
    }
    return vertices;
}
void keys() {
    auto const a=transparentSortKey({1.24f,-0.01f,2.0f});
    auto const b=transparentSortKey({1.25f,-0.26f,2.0f});
    CHECK(a && (*a)[0]==4 && (*a)[1]==-1 && (*a)[2]==8);
    CHECK(b && (*b)[0]==5 && (*b)[1]==-2);
    CHECK(!transparentSortKey({std::numeric_limits<float>::infinity(),0,0}));
    CHECK(!transparentSortKey({0,std::numeric_limits<float>::quiet_NaN(),0}));
    CHECK(!transparentSortKey({std::ldexp(1.0f,61),0,0}));
    auto lower=transparentSortKey({-std::ldexp(1.0f,61),0,0});
    CHECK(lower && (*lower)[0]==(std::numeric_limits<std::int64_t>::min)());
}
void orderAndAttributes() {
    auto p=quads({1,5,3});
    auto const order=transparentQuadOrder(p,{0,0,0});
    CHECK(order && *order==std::vector<std::size_t>({1,2,0}));
    CHECK(transparentQuadOrderChanged(*order));
    std::vector<int> attribute{0,1,2,3,10,11,12,13,20,21,22,23};
    CHECK(reorderQuadVertexField(attribute,*order));
    CHECK(attribute==std::vector<int>({10,11,12,13,20,21,22,23,0,1,2,3}));
    CHECK(reorderQuadVertexField(p,*order));
    auto same=transparentQuadOrder(p,{0,0,0});
    CHECK(same && !transparentQuadOrderChanged(*same));
    auto moved=transparentQuadOrder(p,{0,0,10});
    CHECK(moved && *moved==std::vector<std::size_t>({2,1,0}));
    auto boundary=quads({-17,-16,15,16});
    auto before=transparentQuadOrder(boundary,{0,0,0});
    CHECK(before && *before==std::vector<std::size_t>({0,1,3,2}));
    auto duplicate=quads({-5,5,5});
    auto tie=transparentQuadOrder(duplicate,{0,0,0});
    CHECK(tie && *tie==std::vector<std::size_t>({0,1,2}));
}
void indices() {
    auto p=quads({1,5});
    std::vector<unsigned int> quadIndices{0,1,2,3,4,5,6,7};
    auto quadOrder=transparentIndexedPrimitiveOrder(p,quadIndices,4U,{0,0,0});
    CHECK(quadOrder && *quadOrder==std::vector<std::size_t>({1,0}));
    CHECK(reorderPrimitiveField(quadIndices,*quadOrder,4U));
    CHECK(quadIndices==std::vector<unsigned int>({4,5,6,7,0,1,2,3}));
    std::vector<unsigned int> indices{0,1,2,0,2,3,4,5,6,4,6,7};
    auto const order=transparentIndexedPrimitiveOrder(p,indices,3U,{0,0,0});
    CHECK(order && (*order)[0]>=2U && (*order)[1]>=2U);
    auto original=indices;
    CHECK(reorderPrimitiveField(indices,*order,3U));
    CHECK(std::multiset<unsigned int>(indices.begin(),indices.end())==std::multiset<unsigned int>(original.begin(),original.end()));
    for (std::size_t i=0; i<order->size(); ++i) {
        for (std::size_t c=0; c<3; ++c) CHECK(indices[i*3+c]==original[(*order)[i]*3+c]);
    }
    indices[0]=99;
    CHECK(!transparentIndexedPrimitiveOrder(p,indices,3U,{0,0,0}));
    indices.pop_back();
    CHECK(!transparentIndexedPrimitiveOrder(p,indices,3U,{0,0,0}));
    std::vector<glm::vec3> triangle{{0,0,1},{1,0,1},{0,1,1},{0,0,9},{1,0,9},{0,1,9}};
    auto triangles=transparentPrimitiveOrder(triangle,3U,{0,0,0});
    CHECK(triangles && *triangles==std::vector<std::size_t>({1,0}));
}
void malformed() {
    CHECK(!transparentQuadOrder({}, {0,0,0}));
    auto bad=quads({1}); bad.push_back({0,0,2});
    CHECK(!transparentQuadOrder(bad,{0,0,0}));
    bad.pop_back(); bad[0].z=std::numeric_limits<float>::quiet_NaN();
    CHECK(!transparentQuadOrder(bad,{0,0,0}));
    CHECK(!transparentQuadOrder(quads({1}),{0,std::numeric_limits<float>::infinity(),0}));
    std::vector<std::string> field{"a","b","c","d","e","f","g","h"};
    auto original=field;
    for (auto const& invalid: {std::vector<std::size_t>{1,9}, std::vector<std::size_t>{1,1}, std::vector<std::size_t>{0}}) {
        CHECK(!reorderQuadVertexField(field,invalid));
        CHECK(field==original);
    }
    CHECK(!reorderPrimitiveField(field,std::vector<std::size_t>{1,0},2U));
    std::vector<std::unique_ptr<int>> owned;
    for(int i=0;i<8;++i) owned.push_back(std::make_unique<int>(i));
    CHECK(!reorderQuadVertexField(owned,std::vector<std::size_t>{1,2}));
    for(int i=0;i<8;++i) CHECK(owned[i] && *owned[i]==i);
    CHECK(reorderQuadVertexField(owned,std::vector<std::size_t>{1,0}));
    CHECK(owned[0] && *owned[0]==4 && owned[4] && *owned[4]==0);
    auto extreme=quads({(std::numeric_limits<float>::max)(),-(std::numeric_limits<float>::max)()});
    CHECK(transparentQuadOrder(extreme,{0,0,0}));
}
void randomizedAndBudgetMeasurement() {
    std::mt19937 random(42);
    std::uniform_real_distribution<float> coord(-10000,10000);
    for(int run=0;run<128;++run) {
        std::vector<glm::vec3> p;
        for(int q=0;q<128;++q) {
            auto center=glm::vec3{coord(random),coord(random),coord(random)};
            for(int c=0;c<4;++c) p.push_back(center);
        }
        auto const camera=glm::vec3{coord(random),coord(random),coord(random)};
        auto order=transparentQuadOrder(p,camera);
        CHECK(order && order->size()==128);
        CHECK(std::set<std::size_t>(order->begin(),order->end()).size()==128);
        CHECK(reorderQuadVertexField(p,*order));
        auto identity=transparentQuadOrder(p,camera);
        CHECK(identity && !transparentQuadOrderChanged(*identity));
    }
    std::vector<glm::vec3> p;
    // Worst full 16^3 section without native internal face culling: 24,576 quads.
    for(int q=0;q<24576;++q) {
        auto center=glm::vec3{coord(random),coord(random),coord(random)};
        for(int c=0;c<4;++c) p.push_back(center);
    }
    auto start=std::chrono::steady_clock::now();
    auto order=transparentQuadOrder(p,{0,0,0});
    CHECK(order && order->size()==24576);
    CHECK(reorderQuadVertexField(p,*order));
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    std::cout << "sort_benchmark quads=24576 vertices=98304 sort_and_positions_us=" << us << '\n';
}
}
int main() {
    keys(); orderAndAttributes(); indices(); malformed(); randomizedAndBudgetMeasurement();
    std::cout << "LHoloTranslucencyTests: PASS checks=" << checks << '\n';
}
