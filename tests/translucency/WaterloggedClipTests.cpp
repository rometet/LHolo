#include "projection/core/WaterloggedFaceClip.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using namespace lholo::projection::detail;
namespace {
unsigned checks{};
#define CHECK(condition) do { ++checks; if (!(condition)) {   std::cerr << "FAIL line=" << __LINE__ << " " << #condition << "\n"; std::exit(1); } } while(0)

WaterloggedQuadPositions quad(float plane, int axis, float u0, float u1, float v0, float v1) {
    WaterloggedQuadPositions q{};
    int const a=(axis+1)%3,b=(axis+2)%3;
    for (unsigned i=0;i<4;++i) {
        q[i][axis]=plane;
        q[i][a]=(i==1||i==2)?u1:u0;
        q[i][b]=(i==2||i==3)?v1:v0;
    }
    return q;
}
void near(float a,float b,float eps=.002F){ if(std::fabs(a-b)>eps)std::cerr<<"NEAR values "<<a<<" vs "<<b<<" epsilon="<<eps<<'\n'; CHECK(std::fabs(a-b)<=eps); }

void staircaseTop() {
    auto water=quad(138.998993F,1,123.F,124.F,-45.F,-44.F);
    auto body=quad(138.999893F,1,123.F,124.F,-45.F,-44.5F);
    auto face=waterloggedAxisRect(body);
    CHECK(face);
    auto pieces=clipWaterloggedNativeFace(water,std::span{&*face,1U});
    CHECK(pieces && pieces->size()==1);
    CHECK((*pieces)[0].axis==1);
    near(waterloggedRectArea((*pieces)[0]),.5F);
    // For y-normal faces, U maps to z and V maps to x.
    auto const xMin=(*pieces)[0].v0;
    near(xMin,-44.5F);
    auto original=waterloggedAxisRect(water);
    CHECK(original);
    for(auto const& pt:water) {
        auto sampled=waterloggedCorner(*original,(*pieces)[0],pt);
        CHECK(std::isfinite(sampled[0]));
        CHECK(sampled[1]==pt[1]);
        CHECK(sampled[0]>=-44.5001F && sampled[0]<=-43.9999F);
        auto fraction=waterloggedFraction(*original,sampled);
        CHECK(fraction[1]>=.499F && fraction[1]<=1.001F);
    }
}
void staircaseSide() {
    auto water=quad(-45.F,0,138.F,138.998993F,123.F,124.F);
    auto body=quad(-44.99990F,0,138.0001F,138.5F,123.0001F,123.9999F);
    auto rect=waterloggedAxisRect(body);
    CHECK(rect);
    auto pieces=clipWaterloggedNativeFace(water,std::span{&*rect,1U});
    CHECK(pieces && pieces->size()==1);
    near(waterloggedRectArea((*pieces)[0]),.498993F);
    near((*pieces)[0].u0,138.5F);
}
void fullOcclusion() {
    auto water=quad(0.F,0,0.F,1.F,0.F,1.F);
    auto body=quad(.0001F,0,.0001F,.9999F,.0001F,.9999F);
    auto rect=waterloggedAxisRect(body);
    CHECK(rect);
    auto pieces=clipWaterloggedNativeFace(water,std::span{&*rect,1U});
    CHECK(pieces && pieces->empty());
}
void unrelatedOrUnsafe() {
    auto water=quad(0.F,1,0.F,1.F,0.F,1.F);
    auto other=quad(5.F,1,0.F,1.F,0.F,1.F);
    auto body=waterloggedAxisRect(other);
    CHECK(body);
    CHECK(!clipWaterloggedNativeFace(water,std::span{&*body,1U}));
    CHECK(!clipWaterloggedNativeFace(water,{}));
    auto diagonal=water;
    diagonal[0][1]+=0.4F;
    CHECK(!clipWaterloggedNativeFace(diagonal,std::span{&*body,1U}));
    auto bad=water;
    bad[0][0]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!waterloggedAxisRect(bad));
    CHECK(!clipWaterloggedNativeFace(bad,std::span{&*body,1U}));
}
void multiBody() {
    auto water=quad(0.F,1,0.F,1.F,0.F,1.F);
    auto a=waterloggedAxisRect(quad(.0009F,1,0.F,.25F,0.F,1.F));
    auto b=waterloggedAxisRect(quad(.0009F,1,.25F,.5F,0.F,1.F));
    CHECK(a && b);
    std::array<WaterloggedFaceRect,2> faces{*a,*b};
    auto result=clipWaterloggedNativeFace(water,faces);
    CHECK(result && result->size()==1);
    near(result->front().u0,.5F);
    near(waterloggedRectArea(result->front()),.5F);
}
}
int main(){
    staircaseTop();staircaseSide();fullOcclusion();unrelatedOrUnsafe();multiBody();
    std::cout<<"LHoloWaterloggedClipTests PASS checks="<<checks<<" GPU=NOT_RUN\n";
}
