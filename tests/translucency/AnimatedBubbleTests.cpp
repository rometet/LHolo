#include "projection/core/AnimatedBubbleGeometry.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using namespace lholo::projection::detail;
namespace {
std::size_t checks{};
#define CHECK(x) do { ++checks; if(!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << "\n"; std::exit(1); } } while(0)
using Quads = std::vector<AnimatedBubbleBillboardQuad>;
Quads sample(bool down, double seconds, std::uint32_t seed,
             AnimatedBubbleVec3 camera={2.F,1.5F,3.F}) {
    Quads result;
    emitAnimatedBubbleCell({0.F,0.F,0.F},camera,down,seconds,seed,
        [&](auto const& quad){result.push_back(quad);});
    return result;
}
AnimatedBubbleVec3 avgFirstRing(Quads const& q) {
    AnimatedBubbleVec3 p{};
    for(std::size_t i=0;i<AnimatedBubbleSegments;++i) {
        for(auto const& v:q[i].corners){p.x+=v.x;p.y+=v.y;p.z+=v.z;}
    }
    float n=1.F/static_cast<float>(AnimatedBubbleSegments*4U);
    return {p.x*n,p.y*n,p.z*n};
}
void finiteAndBounded() {
    for(bool down:{false,true}) for(std::uint32_t seed=0;seed<32;++seed)
        for(double clock:{0.,0.01,0.2,1.5,15.999,16.001,3600.,1e7}) {
            auto q=sample(down,clock,seed);
            CHECK(q.size()<=AnimatedBubbleQuadsPerCell);
            CHECK(q.size()%AnimatedBubbleSegments==0);
            for(auto const& face:q) {
                CHECK(std::isfinite(face.opacity) && face.opacity>0.F && face.opacity<=.681F);
                for(auto const& v:face.corners) {
                    CHECK(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
                    CHECK(v.x>0.F && v.x<1.F);
                    CHECK(v.y>0.F && v.y<1.F);
                    CHECK(v.z>0.F && v.z<1.F);
                }
            }
            auto repeated=sample(down,clock,seed);
            CHECK(repeated.size()==q.size());
            for(std::size_t i=0;i<q.size();++i) {
                CHECK(repeated[i].opacity==q[i].opacity);
                for(int j=0;j<4;++j) {
                    CHECK(repeated[i].corners[j].x==q[i].corners[j].x);
                    CHECK(repeated[i].corners[j].y==q[i].corners[j].y);
                    CHECK(repeated[i].corners[j].z==q[i].corners[j].z);
                }
            }
        }
}
void movementAndContinuity() {
    std::size_t testedUp{},testedDown{},continuity{};
    for(std::uint32_t seed=0;seed<30;++seed)
        for(double time=0.12;time<3.0;time+=0.11) {
            auto up=sample(false,time,seed);
            auto later=sample(false,time+.018,seed);
            if(up.size()==AnimatedBubbleQuadsPerCell && later.size()==AnimatedBubbleQuadsPerCell) {
                auto a=avgFirstRing(up),b=avgFirstRing(later);
                if (std::fabs(b.y-a.y)<.08F) {
                    CHECK(b.y>a.y && b.y-a.y<.025F);
                    ++testedUp;
                } else {
                    CHECK(std::min(up[0].opacity,later[0].opacity)<.15F);
                }
            }
            auto down=sample(true,time,seed),downLater=sample(true,time+.018,seed);
            if(down.size()==AnimatedBubbleQuadsPerCell && downLater.size()==AnimatedBubbleQuadsPerCell) {
                auto a=avgFirstRing(down),b=avgFirstRing(downLater);
                if (std::fabs(a.y-b.y)<.08F) {
                    CHECK(b.y<a.y && a.y-b.y<.025F);
                    ++testedDown;
                } else {
                    CHECK(std::min(down[0].opacity,downLater[0].opacity)<.15F);
                }
            }
            auto before=sample(false,15.999,seed),after=sample(false,16.001,seed);
            if(before.size()==AnimatedBubbleQuadsPerCell
                && after.size()==AnimatedBubbleQuadsPerCell) {
                auto a=avgFirstRing(before),b=avgFirstRing(after);
                CHECK(std::fabs(a.y-b.y)<.02F);
                ++continuity;
            }
        }
    CHECK(testedUp>20 && testedDown>20 && continuity>10);
}
void billboardFacing() {
    for(auto camera: {AnimatedBubbleVec3{2.F,1.F,3.F}, AnimatedBubbleVec3{-.3F,.3F,-4.F},
                       AnimatedBubbleVec3{0.F,4.F,0.F}}) {
        auto q=sample(false,.54,5,camera);
        CHECK(!q.empty());
        for(auto const& face:q) {
            auto const a=face.corners[0],b=face.corners[1],d=face.corners[3];
            auto const normal=bubbleCross(bubbleSubtract(b,a),bubbleSubtract(d,a));
            auto const towards=bubbleSubtract(camera,a);
            CHECK(std::isfinite(bubbleDot(normal,towards)));
            CHECK(bubbleDot(normal,towards)>-0.0001F);
        }
    }
}
void nonFiniteSafe() {
    auto q=sample(false,std::numeric_limits<double>::infinity(),0);
    CHECK(q.empty());
    q=sample(false,std::numeric_limits<double>::quiet_NaN(),0);
    CHECK(q.empty());
    CHECK(AnimatedBubbleMaxVisibleCells * AnimatedBubbleVerticesPerCell<=10000U);
}
}
int main() {
    finiteAndBounded();
    movementAndContinuity();
    billboardFacing();
    nonFiniteSafe();
    std::cout<<"LHoloAnimatedBubbleTests PASS checks="<<checks
             <<" particlesPerCell="<<AnimatedBubblePerCell
             <<" maxVisibleVertices="<<AnimatedBubbleMaxVisibleCells*AnimatedBubbleVerticesPerCell
             <<" native_gpu=NOT_RUN\n";
}
