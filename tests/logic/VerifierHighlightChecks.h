#pragma once
#include "projection/runtime/VerifierHighlightRules.h"

namespace lholo::tests {
template<class Check>
void runVerifierHighlightChecks(Check&& check) {
    using namespace projection::detail;
    using namespace structure;
    schematic::SelectedMistake target;
    target.stamp.worldEpoch=11;target.stamp.dimension=2;target.stamp.sessionRevision=3;
    target.stamp.placementId=4;target.stamp.loadedGeneration=5;target.stamp.reportRevision=6;
    target.stamp.placementOrigin={-17,64,23};target.stamp.placementRotation=1;target.stamp.placementMirror=2;
    target.mismatch={VerificationState::WrongType,{-12,65,26},"minecraft:stone","minecraft:dirt",9};
    VerifierHighlightContext context{11,2,5,{-17,64,23},1,2,LayerAxis::Y,LayerDisplayMode::All,0,true,true};
    check(verifierHighlightPosition(target,context)==std::optional<std::array<int,3>>{{-12,65,26}});
    auto invalid=context;invalid.worldEpoch++;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.dimension++;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.loadedGeneration++;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.origin.x++;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.origin.y++;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.origin.z++;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.rotation=2;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.mirror=0;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.layerAxis=LayerAxis::X;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.layerMode=LayerDisplayMode::Single;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.layer++;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.visible=false;check(!verifierHighlightPosition(target,invalid));
    invalid=context;invalid.countExtras=false;check(!verifierHighlightPosition(target,invalid));
    auto changed=target;changed.stamp.placementId=0;check(!verifierHighlightPosition(changed,context));
    changed=target;changed.mismatch.world.x=(std::numeric_limits<std::int64_t>::max)();check(!verifierHighlightPosition(changed,context));
    changed=target;changed.mismatch.world.y=(std::numeric_limits<int>::max)();check(!verifierHighlightPosition(changed,context));
    changed=target;changed.mismatch.world.z=(std::numeric_limits<int>::min)();check(!verifierHighlightPosition(changed,context));
    // WrongType and Extra retain the existing shared512 nearest rows. Full
    // counts still include an Extra cell displaced by nearer WrongType cells.
    VerificationTally tally;
    std::vector<Mismatch> retained;
    bool truncated{};
    for(int i=0;i<513;++i) {
        tally.add(VerificationState::WrongType,true);
        truncated=retainNearest(retained,{VerificationState::WrongType,{i,0,0},{},{},static_cast<double>(i)*i},512)||truncated;
    }
    tally.add(VerificationState::Extra,false);
    truncated=retainNearest(retained,{VerificationState::Extra,{2000,0,0},{},{},4000000},512)||truncated;
    std::sort(retained.begin(),retained.end(),nearerMismatch);
    check(truncated && retained.size()==512 && tally.wrongType==513 && tally.extra==1);
    check(std::none_of(retained.begin(),retained.end(),[](auto const& row){return row.kind==VerificationState::Extra;}));
    check(!nextMistake(retained,MistakeFilter::Extra,{}));
    check(nextMistake(retained,MistakeFilter::WrongType,{})==0);
}
} // namespace lholo::tests
