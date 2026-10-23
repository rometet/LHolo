#include "projection/core/WaterloggedFaceClip.h"
#include "projection/core/WaterloggedClipFamily.h"
#include "projection/core/ProjectionBubbleGlyph.h"
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
void safeFamilyPolicy() {
    CHECK(waterloggedClipCandidate("minecraft:oak_stairs") == WaterloggedClipFamily::Stairs);
    CHECK(waterloggedSafeClipFamily("minecraft:quartz_stairs",true) == WaterloggedClipFamily::Stairs);
    CHECK(waterloggedSafeClipFamily("minecraft:quartz_stairs",false) == WaterloggedClipFamily::None);
    for (auto const& name: {"minecraft:oak_slab","minecraft:stone_slab",
                             "minecraft:wooden_slab","minecraft:stone_block_slab2",
                             "minecraft:stone_block_slab3","minecraft:stone_block_slab4"}) {
        CHECK(waterloggedSafeClipFamily(name,true) == WaterloggedClipFamily::Slab);
        CHECK(waterloggedSafeClipFamily(name,false) == WaterloggedClipFamily::None);
    }
    CHECK(waterloggedSafeClipFamily("minecraft:trapdoor",true) == WaterloggedClipFamily::Trapdoor);
    CHECK(waterloggedSafeClipFamily("minecraft:oak_trapdoor",true) == WaterloggedClipFamily::Trapdoor);
    CHECK(waterloggedSafeClipFamily("minecraft:iron_trapdoor",false) == WaterloggedClipFamily::None);
    CHECK(waterloggedSafeClipFamily("minecraft:oak_trapdoor",false) == WaterloggedClipFamily::None);
    for (auto const& name: {"minecraft:glass_pane","minecraft:water","minecraft:lava",
                             "minecraft:flowing_water","minecraft:flowing_lava",
                             "minecraft:bubble_column","minecraft:kelp","minecraft:oak_fence",
                             "othermod:stone_slab"}) {
        CHECK(waterloggedClipCandidate(name) == WaterloggedClipFamily::None);
    }
}
void slabGeometry() {
    auto const waterSide=quad(0.F,0,0.F,1.F,0.F,1.F);
    auto const slabSolidSide=waterloggedAxisRect(quad(.0009F,0,0.F,.5F,0.F,1.F));
    CHECK(slabSolidSide);
    auto clipped=clipWaterloggedNativeFace(waterSide,std::span{&*slabSolidSide,1U});
    CHECK(clipped && clipped->size()==1);
    near(waterloggedRectArea((*clipped)[0]),.5F);
    near((*clipped)[0].u0,.5F);
    // Slab body top at half-height is not near full water source surface.
    auto waterTop=quad(.99899F,1,0.F,1.F,0.F,1.F);
    auto slabTop=waterloggedAxisRect(quad(.5001F,1,0.F,1.F,0.F,1.F));
    CHECK(slabTop);
    CHECK(!clipWaterloggedNativeFace(waterTop,std::span{&*slabTop,1U}));
    // Bottom slab fully hides the face coincident with the bottom of water.
    auto bottomWater=quad(0.F,1,0.F,1.F,0.F,1.F);
    auto bottomSlab=waterloggedAxisRect(quad(.0009F,1,0.F,1.F,0.F,1.F));
    CHECK(bottomSlab);
    auto bottom=clipWaterloggedNativeFace(bottomWater,std::span{&*bottomSlab,1U});
    CHECK(bottom && bottom->empty());
}
void opaqueTrapdoorGeometry() {
    auto const waterSide=quad(0.F,0,0.F,1.F,0.F,1.F);
    auto const solidOpaqueFace=waterloggedAxisRect(quad(.0009F,0,.8F,.9F,0.F,1.F));
    CHECK(solidOpaqueFace);
    auto clipped=clipWaterloggedNativeFace(waterSide,std::span{&*solidOpaqueFace,1U});
    CHECK(clipped && clipped->size()==2U);
    float area{};
    for (auto const& rect : *clipped) area += waterloggedRectArea(rect);
    near(area,.9F);
    // Perforated/alpha-test trapdoors may have a rectangular native
    // outline, but the family guard must reject them before this stage.
    CHECK(waterloggedSafeClipFamily("minecraft:oak_trapdoor",false)
          == WaterloggedClipFamily::None);
}
}
int main(){
    staircaseTop();staircaseSide();fullOcclusion();unrelatedOrUnsafe();multiBody();
    safeFamilyPolicy();slabGeometry();opaqueTrapdoorGeometry();
    // Captured native R9 waterlogged trapdoor faces are ~0.001 apart;
    // keep alpha-test holes and move water behind the body instead of culling.
    auto waterSide=quad(-44.999001F,0,138.F,138.999F,147.F,148.F);
    auto bodySide=waterloggedAxisRect(quad(-45.F,0,138.F,138.1825F,147.F,148.F));
    CHECK(bodySide);
    std::array<float,3> cellCenter{-44.5F,138.5F,147.5F};
    auto inset=waterloggedCutoutInset(waterSide,std::span{&*bodySide,1U},cellCenter);
    CHECK(inset && *inset > 0.F);
    near(*inset,WaterloggedCutoutInsetDistance);
    auto otherSide=quad(-44.000999F,0,138.F,138.999F,147.F,148.F);
    CHECK(!waterloggedCutoutInset(otherSide,std::span{&*bodySide,1U},cellCenter));
    auto topWater=quad(138.998993F,1,149.F,150.F,-45.F,-44.F);
    auto topTrap=waterloggedAxisRect(quad(139.F,1,149.F,150.F,-45.F,-44.8175F));
    CHECK(topTrap);
    std::array<float,3> topCenter{-44.5F,138.5F,149.5F};
    auto topInset=waterloggedCutoutInset(topWater,std::span{&*topTrap,1U},topCenter);
    CHECK(topInset && *topInset < 0.F);
    near(*topInset,-WaterloggedCutoutInsetDistance);
    auto farFace=waterloggedAxisRect(quad(138.3F,1,149.F,150.F,-45.F,-44.F));
    CHECK(farFace);
    CHECK(!waterloggedCutoutInset(topWater,std::span{&*farFace,1U},topCenter));
    CHECK(!waterloggedCutoutInset(topWater,{},topCenter));
    // Static bubble indicators must stay inside their cell and cannot spawn particles.
    for (bool down : {false,true}) {
        std::size_t glyphQuads{};
        float minY=1.F,maxY=0.F;
        bool allInCell=true;
        emitBubbleColumnGlyph({-45.F,138.F,153.F},down,
            [&](BubbleGlyphQuad const& glyph) {
                ++glyphQuads;
                for(auto const& p : glyph) {
                    allInCell &= std::isfinite(p[0]) && std::isfinite(p[1])
                        && std::isfinite(p[2]);
                    allInCell &= p[0] > -45.F && p[0] < -44.F
                        && p[1] > 138.F && p[1] < 139.F
                        && p[2] > 153.F && p[2] < 154.F;
                    minY=std::min(minY,p[1]-138.F);
                    maxY=std::max(maxY,p[1]-138.F);
                }
            });
        CHECK(glyphQuads == BubbleGlyphQuadsPerCell);
        CHECK(glyphQuads * 4U == BubbleGlyphVerticesPerCell);
        CHECK(allInCell);
        CHECK(minY > .1F && maxY < .9F);
    }
    std::cout<<"LHoloWaterloggedClipTests PASS checks="<<checks<<" GPU=NOT_RUN\n";
}
