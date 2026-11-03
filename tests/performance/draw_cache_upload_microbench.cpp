#include "projection/core/ProjectionDrawCache.h"
#include <chrono>
#include <cstdio>
#include <vector>
using namespace lholo::projection::detail;
int main() {
 constexpr std::size_t count=65536, updates=256;
 std::vector<ProjectionDrawRecord> rows(count);
 for (std::size_t i=0;i<count;++i) rows[i]={i,{float(i%256),float(i/256),0.f},0};
 ProjectionDrawCache oldCache, newCache;
 std::size_t oldReads{},newReads{};
 auto const start=std::chrono::steady_clock::now();
 for(std::size_t i=0;i<updates;++i){
   auto section=(i*8191)%count;
   rows[section].slots=(i%8==0?projectionDrawBit(ProjectionDrawSlot::Blend):
       projectionDrawBit(ProjectionDrawSlot::Opaque));
   oldCache.prepare(1,i+1,i+1,count,{1,2,3},
       [&](std::size_t j){++oldReads;return rows[j];},
       [](std::size_t){return true;});
 }
 auto const baselineEnd=std::chrono::steady_clock::now();
 // Initialize the optimized cache with the same all-empty initial state.
 for(auto& r:rows) r.slots=0;
 for(std::size_t i=0;i<updates;++i){
   auto section=(i*8191)%count;
   rows[section].slots=(i%8==0?projectionDrawBit(ProjectionDrawSlot::Blend):
       projectionDrawBit(ProjectionDrawSlot::Opaque));
   newCache.noteMeshUpload(section);
   newCache.prepare(1,i+1,i+1,count,{1,2,3},
       [&](std::size_t j){++newReads;return rows[j];},
       [](std::size_t){return true;});
 }
 auto const optimizedEnd=std::chrono::steady_clock::now();
 bool matching=oldCache.transparentTerrain==newCache.transparentTerrain;
 for(std::size_t slot=0;slot<kProjectionDrawSlotCount;++slot){
   matching &= oldCache.lists[slot]==newCache.lists[slot];
 }
 auto ms=[](auto a, auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
 std::printf("sections=%zu updates=%zu full_ms=%.3f delta_ms=%.3f full_reads=%zu delta_reads=%zu lists_equal=%d\n",
     count,updates,ms(start,baselineEnd),ms(baselineEnd,optimizedEnd),oldReads,newReads,int(matching));
 return matching&&newReads<count+updates?0:1;
}