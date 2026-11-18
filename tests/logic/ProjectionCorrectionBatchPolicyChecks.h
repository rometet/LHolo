#pragma once
#include "projection/mesh/ProjectionCorrectionBatchPolicy.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>
namespace lholo::tests {
template<class Check> void runCorrectionBatchPolicyChecks(Check check) {
    using namespace lholo::projection::detail;
    check(!correctionBatchHasVisibleMember(0,0,[](auto){return true;}));
    check(!correctionBatchHasVisibleMember(3,64,[](auto){return true;}));
    check(!correctionBatchHasVisibleMember(64,32,[](auto){return true;}));
    check(correctionBatchHasVisibleMember(32,64,[](auto i){return i == 63;}));
    check(!correctionBatchHasVisibleMember(32,64,[](auto){return false;}));
    // A batched group can include off-frustum source geometry; that geometry
    // is clipped by the GPU. The visible source members must each be rendered
    // exactly once, regardless of frustum patterns and cache readiness.
    std::mt19937 rng{0x26B47C5u};
    for (std::size_t count : {1u,7u,8u,9u,15u,16u,31u,32u,33u,63u,64u,96u,128u,393u,1024u}) {
        for (int scenario=0;scenario<240;++scenario) {
            std::vector<unsigned char> visible(count);
            std::vector<unsigned char> included(count);
            for (std::size_t i=0;i<count;++i) {
                visible[i]=static_cast<unsigned char>(scenario % 5 == 0 ? 1
                    : scenario % 5 == 1 ? (i%3!=0)
                    : scenario % 5 == 2 ? ((i / kCorrectionBatchSize)%2!=0)
                    : (rng()%3 != 0));
                included[i]=static_cast<unsigned char>(rng()%7 != 0);
            }
            // Each submitted batch covers every eligible source section
            // in its group, including those outside the view; each single
            // submission covers just the original visible section.
            std::vector<unsigned char> submissionCounts(count);
            std::size_t lastBatch=static_cast<std::size_t>(-1);
            std::size_t batches=0;
            for (std::size_t i=0;i<count;++i) {
                if (!visible[i] || !included[i]) continue;
                auto const group=i/kCorrectionBatchSize;
                if (group==lastBatch) continue;
                auto const first=group*kCorrectionBatchSize;
                bool const hasVisible=correctionBatchHasVisibleMember(
                    first,count,[&](std::size_t j) { return visible[j] && included[j]; });
                bool const cacheValid=scenario % 7 != 0;
                if (hasVisible && cacheValid) {
                    lastBatch=group;
                    ++batches;
                    for (auto j=first;j<std::min(first+kCorrectionBatchSize,count);++j)
                        if(included[j])++submissionCounts[j];
                } else ++submissionCounts[i];
            }
            for (std::size_t i=0;i<count;++i) {
                check(!included[i] ? submissionCounts[i] == 0
                    : visible[i] ? submissionCounts[i] == 1
                    : submissionCounts[i] <= 1);
            }
            check(batches <= (count+kCorrectionBatchSize-1)/kCorrectionBatchSize);
        }
    }
}
}
