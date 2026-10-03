#pragma once
#include "structure/ManualVerificationControl.h"
#include "structure/VerificationGroups.h"
#include "structure/StructureSession.h"

namespace lholo::tests {
template<class Check> void runManualVerificationChecks(Check check) {
    using namespace structure;
    using namespace structure::schematic;
    ReportStamp context;
    context.worldEpoch=11;context.dimension=2;context.sessionRevision=7;
    context.placementId=4;context.loadedGeneration=12;context.reportRevision=1;
    ManualVerificationControl control;
    check(control.phase()==VerificationPhase::NotVerified);
    for(int tick=0;tick<500;++tick)check(!control.start(context));
    check(control.request(context));check(control.phase()==VerificationPhase::Queued);
    check(!control.request(context));
    auto token=control.start(context);check(token && control.current(token,context));
    check(!control.request(context));check(!control.start(context));
    auto filtered=context;filtered.filterRevision=99;filtered.reportRevision=23;
    check(control.current(token,filtered)); // selection/filter/UI do not scan
    check(control.finish(token,filtered));check(control.phase()==VerificationPhase::Completed);
    for(int tick=0;tick<500;++tick)check(!control.start(context));
    check(!control.finish(token,context));
    check(control.request(context));auto cancelled=control.start(context);
    control.cancel();check(control.phase()==VerificationPhase::Cancelled);
    check(!control.current(cancelled,context));check(!control.finish(cancelled,context));
    check(control.request(context));control.cancel();check(!control.start(context));
    check(control.request(context));auto invalidated=control.start(context);
    for(int field=0;field<5;++field){
        auto changed=context;
        if(field==0)++changed.worldEpoch;
        if(field==1)++changed.dimension;
        if(field==2)++changed.sessionRevision;
        if(field==3)++changed.placementId;
        if(field==4)++changed.loadedGeneration;
        check(!control.current(invalidated,changed));check(!control.finish(invalidated,changed));
    }
    control.invalidate();check(control.phase()==VerificationPhase::NotVerified);
    check(!control.finish(invalidated,context));check(!control.start(context));
    auto loading=context;loading.loadedGeneration=0;
    check(control.request(loading));check(!control.start(loading));
    for(int tick=0;tick<100;++tick)check(control.phase()==VerificationPhase::Queued);
    auto loaded=context;loaded.loadedGeneration=45;
    auto waited=control.start(loaded);check(waited && control.current(waited,loaded));
    check(control.finish(waited,loaded));
    auto other=context;++other.dimension;
    check(control.request(context));check(!control.start(other));control.invalidate();
    check(!control.request({}));
    std::vector<Mismatch> rows{
        {VerificationState::Missing,{1,2,3},"stone","air",1},
        {VerificationState::WrongType,{4,5,6},"stone","dirt",2},
        {VerificationState::Missing,{7,8,9},"stone","air",3},
        {VerificationState::Extra,{9,8,7},"air","stone",4}};
    auto groups=groupMismatches(rows,MistakeFilter::Mistakes);
    check(groups.size()==3);check(groups[0].indices==std::vector<std::size_t>{0,2});
    check(groups[1].indices==std::vector<std::size_t>{1});
    check(groups[2].indices==std::vector<std::size_t>{3});
    auto missing=groupMismatches(rows,MistakeFilter::Missing);
    check(missing.size()==1 && missing[0].indices.size()==2);
    check(groupMismatches(rows,MistakeFilter::WrongState).empty());
    auto& owner=detail::StructureSession::getInstance();
    auto first=std::make_shared<LoadedStructure>();first->generation=101;
    auto second=std::make_shared<LoadedStructure>();second->generation=102;
    owner.replaceLoaded(first,"first",{});
    auto initial=owner.snapshot();int publications{};
    check(owner.publishVerificationIfCurrent(initial,[&]{++publications;return true;}));
    check(publications==1);
    owner.replaceLoaded(second,"second",{});
    check(!owner.publishVerificationIfCurrent(initial,[&]{++publications;return true;}));
    auto current=owner.snapshot();
    owner.setOffsetX(current.transform.offsetX+1);
    check(!owner.publishVerificationIfCurrent(current,[&]{++publications;return true;}));
    current=owner.snapshot();
    owner.clearLoaded({});
    check(!owner.publishVerificationIfCurrent(current,[&]{++publications;return true;}));
    check(publications==1);
    owner.resetTransform();
}
} // namespace lholo::tests
