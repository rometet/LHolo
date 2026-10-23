#include "projection/core/VanillaBubbleSprite.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using namespace lholo::projection::detail;
namespace {
std::size_t checks{};
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<"FAIL line="<<__LINE__<<" "<<#x<<"\n";std::exit(1);}} while(0)
using Sprites=std::vector<VanillaBubbleSprite>;
Sprites sample(bool down,double t,unsigned seed,AnimatedBubbleVec3 cam={2.F,2.F,3.F}){
 Sprites out;
 emitVanillaBubbleSprites({0.F,0.F,0.F},cam,down,t,seed,[&](auto const& q){out.push_back(q);});
 return out;
}
void atlasUv(){
 CHECK(VanillaBubbleParticleAtlasWidth==128.F);
 CHECK(VanillaBubbleParticleAtlasHeight==128.F);
 CHECK(VanillaBubbleU0==0.F);
 CHECK(VanillaBubbleV0==.125F);
 CHECK(VanillaBubbleU1==.0625F);
 CHECK(VanillaBubbleV1==.1875F);
 CHECK((VanillaBubbleU1-VanillaBubbleU0)*128.F==8.F);
 CHECK((VanillaBubbleV1-VanillaBubbleV0)*128.F==8.F);
}
void facesAndRanges(){
 for(bool down:{false,true})for(unsigned seed=0;seed<64;++seed)
 for(double t:{0.,.02,.15,.5,1.,2.,7.2,15.999,16.001,10000.,1e8}){
  auto sprites=sample(down,t,seed);
  CHECK(sprites.size()<=AnimatedBubblePerCell);
  for(auto const& s:sprites){
   CHECK(std::isfinite(s.opacity));
   CHECK(s.opacity>0.F && s.opacity<=1.F);
   for(auto const& p:s.vertices){
    CHECK(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z));
    CHECK(p.x>0.F&&p.x<1.F);
    CHECK(p.y>0.F&&p.y<1.F);
    CHECK(p.z>0.F&&p.z<1.F);
   }
  }
  auto rep=sample(down,t,seed);
  CHECK(rep.size()==sprites.size());
  for(std::size_t i=0;i<sprites.size();++i){
   CHECK(rep[i].opacity==sprites[i].opacity);
   for(std::size_t j=0;j<4;++j){
    CHECK(rep[i].vertices[j].x==sprites[i].vertices[j].x);
    CHECK(rep[i].vertices[j].y==sprites[i].vertices[j].y);
    CHECK(rep[i].vertices[j].z==sprites[i].vertices[j].z);
   }
  }
 }
}
float avgY(VanillaBubbleSprite const& s) {
 float sum{};for(auto const& v:s.vertices)sum+=v.y;return sum*.25F;
}
void movement(){
 std::size_t upward{},downward{};
 for(unsigned seed=0;seed<48;++seed)for(double t=.1;t<2.9;t+=.09){
  auto s=sample(false,t,seed),next=sample(false,t+.014,seed);
  if(s.size()==AnimatedBubblePerCell&&next.size()==AnimatedBubblePerCell){
   float const diff=avgY(next.front())-avgY(s.front());
   if(std::fabs(diff)<.1F){CHECK(diff>0.F&&diff<.03F);++upward;}
  }
  s=sample(true,t,seed);next=sample(true,t+.014,seed);
  if(s.size()==AnimatedBubblePerCell&&next.size()==AnimatedBubblePerCell){
   float const diff=avgY(next.front())-avgY(s.front());
   if(std::fabs(diff)<.1F){CHECK(diff<0.F&&diff>-.03F);++downward;}
  }
 }
 CHECK(upward>25);CHECK(downward>25);
}
void quadArea(){
 for(auto cam:{AnimatedBubbleVec3{2.F,1.5F,4.F},AnimatedBubbleVec3{-2.F,2.F,-2.F}}){
  auto sprites=sample(false,.6,2,cam);
  CHECK(!sprites.empty());
  for(auto const& s:sprites){
   auto e=bubbleSubtract(s.vertices[1],s.vertices[0]);
   auto f=bubbleSubtract(s.vertices[3],s.vertices[0]);
   auto normal=bubbleCross(e,f);
   float const area=std::sqrt(bubbleDot(normal,normal));
   CHECK(area>.00002F && area<.08F);
  }
 }
}
void nonfinite(){
 auto a=sample(false,std::numeric_limits<double>::infinity(),1);
 CHECK(a.empty());
 auto b=sample(false,std::numeric_limits<double>::quiet_NaN(),1);
 CHECK(b.empty());
}
}
int main(){
 atlasUv();facesAndRanges();movement();quadArea();nonfinite();
 std::cout<<"LHoloVanillaBubbleSpriteTests PASS checks="<<checks
          <<" uv=(0,16,8,8)/128 native_gpu=NOT_RUN\n";
}
