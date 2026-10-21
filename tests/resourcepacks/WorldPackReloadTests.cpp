#include "resourcepacks/WorldPackReloadGpuOwner.h"
#include "projection/mesh/ResourceReloadCancellation.h"
#include <d3d11on12.h>
#include <dxgi1_4.h>
#include <chrono>
#include <barrier>
#include <cstdio>
#include <future>
#include <cstring>
#include <thread>
using namespace lholo;
using namespace resourcepacks;
extern "C" int lholo_resource_reload_v4(WorldPackReloadCommand,WorldPackReloadRequest const*,WorldPackReloadReply*) noexcept;
extern "C" int lholo_resource_reload_v3(WorldPackReloadCommand,WorldPackReloadRequest const*,WorldPackReloadReply*) noexcept;
int checks{},failures{};
void check(bool value,char const* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
WorldPackReloadRequest attempt(std::uint64_t token=1,std::uint64_t oldEpoch=1,std::uint64_t nextEpoch=2) {
    return {sizeof(WorldPackReloadRequest),WorldPackReloadAbi,token,923,456,789,oldEpoch,nextEpoch};
}
bool command(State& owner,WorldPackReloadCommand cmd,WorldPackReloadRequest const& request,WorldPackReloadReply& reply) {
    reply={};return owner.call(cmd,&request,&reply)!=0;
}
void initialize(State& owner,std::shared_ptr<void> const& module) {
    check(owner.enable(module),"enable owned participant");owner.observe({123,456,789,111});
}
void protocol() {
    check(sizeof(WorldPackReloadRequest)==56 && sizeof(WorldPackReloadReply)==88,"versioned v4 ABI layout");
    State owner;auto module=std::make_shared<int>(1);initialize(owner,module);
    auto request=attempt();WorldPackReloadReply reply;WorldPackReloadRequest bootstrap;
    check(command(owner,WorldPackReloadCommand::Query,bootstrap,reply) && reply.epoch==1 && reply.world==0 && reply.client==456,"bootstrap exposes no invented coordinator world stamp");
    auto bad=request;bad.abi=2;
    check(!command(owner,WorldPackReloadCommand::Pause,bad,reply) && reply.phase==WorldPackReloadPhase::Unknown,"old ABI cannot admit");
    reply={};reply.bytes=4;auto before=reply;
    check(!owner.call(WorldPackReloadCommand::Pause,&request,&reply) && reply.bytes==before.bytes,"short declared reply is not overwritten");
    bad=request;bad.client=457;check(!command(owner,WorldPackReloadCommand::Pause,bad,reply),"wrong client owner rejected");
    bad=request;bad.nextEpoch=1;check(!command(owner,WorldPackReloadCommand::Pause,bad,reply),"epoch must advance");
    bad=request;bad.token=0;check(!command(owner,WorldPackReloadCommand::Pause,bad,reply),"zero token rejected");
    std::optional<State::Lease> worker{owner.enter(Role::MeshJob)};
    std::optional<State::Lease> reader{owner.enter(Role::Projection)};
    check(command(owner,WorldPackReloadCommand::Pause,request,reply),"pause closes admission");
    check(reply.world==923,"copied coordinator stamp binds and echoes independently of Level address");
    check(reply.admissionClosed && reply.oldOwnersRetained && reply.asyncJobs==1 && reply.cpuReaders==1 && reply.nativeBorrowers==2,"actual leases counted");
    check(!owner.enter(Role::MeshJob) && !owner.enter(Role::IconDecode),"new native work blocked");
    check(command(owner,WorldPackReloadCommand::Pause,request,reply),"same pause idempotent");
    check(!command(owner,WorldPackReloadCommand::Pause,attempt(2),reply),"overlapping attempt rejected");
    auto query=std::async(std::launch::async,[&] {WorldPackReloadReply r;return command(owner,WorldPackReloadCommand::Query,request,r);});
    check(query.wait_for(std::chrono::seconds{1})==std::future_status::ready && query.get(),"Query does not wait for worker/native readers");
    check(!owner.mayPublish(*worker),"old admitted worker cannot publish after pause");
    check(!command(owner,WorldPackReloadCommand::Prepare,request,reply),"CPU borrow blocks prepare");
    reader.reset();worker.reset();
    check(command(owner,WorldPackReloadCommand::Prepare,request,reply) && reply.phase==WorldPackReloadPhase::Prepared,"no-owned-submission drained participant prepared");
    check(command(owner,WorldPackReloadCommand::Prepare,request,reply),"prepare idempotent");
    check(!command(owner,WorldPackReloadCommand::NativeSettled,request,reply),"settlement cannot precede native begin");
    check(command(owner,WorldPackReloadCommand::BeginNative,request,reply) && reply.oldOwnersRetained,"native starts with old owners retained");
    check(command(owner,WorldPackReloadCommand::BeginNative,request,reply),"native begin idempotent");
    check(!command(owner,WorldPackReloadCommand::CancelBeforeNative,request,reply),"cannot cancel after native begins");
    check(command(owner,WorldPackReloadCommand::NativeSettled,request,reply) && reply.epoch==2 && reply.phase==WorldPackReloadPhase::Reacquiring,"settlement advances resource epoch");
    check(!command(owner,WorldPackReloadCommand::Resume,request,reply),"resume requires owner rebuild");
    auto serial=owner.snapshot()->serial;
    std::optional<State::Lease> rebuild{owner.enter(Role::Projection,true)};
    check(rebuild && rebuild->epoch==2 && owner.mayPublish(*rebuild),"only new epoch can rebuild");
    owner.retiredOwner(Domain::Projection,serial);owner.rebuilt(Domain::Projection,serial,true);
    owner.retiredOwner(Domain::Icons,serial);owner.rebuilt(Domain::Icons,serial,true);
    check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Reacquiring,"ready waits for real rebuild borrow release");
    rebuild.reset();
    check(command(owner,WorldPackReloadCommand::Query,request,reply) && reply.phase==WorldPackReloadPhase::Ready && !reply.oldOwnersRetained,"both owned domains ready after drain");
    bad=request;bad.oldEpoch=2;check(!command(owner,WorldPackReloadCommand::Query,bad,reply),"Query validates attempt old epoch");
    check(command(owner,WorldPackReloadCommand::Resume,request,reply) && !reply.admissionClosed,"resume reopens admission");
    check(command(owner,WorldPackReloadCommand::Resume,request,reply),"resume repeat idempotent");
    check(!command(owner,WorldPackReloadCommand::Pause,attempt(1,2,3),reply),"used native token cannot be replayed");
}
void cancellationAndGpu() {
    State owner;auto module=std::make_shared<int>(2);initialize(owner,module);
    owner.submitted(Domain::Projection);owner.submitted(Domain::Icons);
    auto request=attempt();WorldPackReloadReply reply;
    check(command(owner,WorldPackReloadCommand::Pause,request,reply),"submitted owner pause");
    auto serial=owner.snapshot()->serial;
    check(!command(owner,WorldPackReloadCommand::Prepare,request,reply) && reply.ownedGpu==OwnedGpuFence::Unknown,"CPU zero is not GPU completion");
    owner.gpuReceipt(Domain::Icons,serial,OwnedGpuFence::Complete);
    check(!command(owner,WorldPackReloadCommand::Prepare,request,reply),"icon receipt cannot complete native FrameBuilder mesh");
    check(!owner.gpuReceipt(Domain::Projection,serial-1,OwnedGpuFence::Complete),"stale GPU receipt rejected");
    check(command(owner,WorldPackReloadCommand::CancelBeforeNative,request,reply) && reply.phase==WorldPackReloadPhase::Cancelled,"before-native cancellation retains same owners");
    check(command(owner,WorldPackReloadCommand::ResumeCancelled,request,reply) && reply.epoch==1,"cancel resumes unchanged epoch");
    check(command(owner,WorldPackReloadCommand::ResumeCancelled,request,reply),"cancel resume repeat idempotent");
    check(owner.snapshot()->domainGpu[0]==OwnedGpuFence::Unknown,"cancel does not invent GPU receipt");
    check(!command(owner,WorldPackReloadCommand::Pause,attempt(1),reply),"cancelled attempt token also stays unique");
    check(command(owner,WorldPackReloadCommand::Pause,attempt(2),reply),"new attempt after cancel");
    // This injection tests the protocol only. Production has no native mesh
    // completion receipt and cannot promote Unknown in the projection domain.
    serial=owner.snapshot()->serial;owner.gpuReceipt(Domain::Projection,serial,OwnedGpuFence::Complete);
    request=attempt(2);check(command(owner,WorldPackReloadCommand::Prepare,request,reply),"protocol-only injected native receipt");
    check(command(owner,WorldPackReloadCommand::BeginNative,request,reply),"protocol-only native begin");
    check(command(owner,WorldPackReloadCommand::NativeSettled,request,reply),"protocol-only settlement");
    serial=owner.snapshot()->serial;owner.rebuilt(Domain::Projection,serial,false);
    check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Reacquiring,"failed rebuild stays closed");
    check(!command(owner,WorldPackReloadCommand::CancelBeforeNative,request,reply),"settled failure cannot fake old epoch cancel");
    check(!command(owner,WorldPackReloadCommand::Pause,attempt(3,1,3),reply),"rollback uses current settled epoch");
    auto rollback=attempt(3,2,3);check(command(owner,WorldPackReloadCommand::Pause,rollback,reply),"rollback is another normal token and epoch");
    check(!owner.gpuReceipt(Domain::Icons,serial,OwnedGpuFence::Complete),"previous epoch receipt cannot leak into rollback");
}
void invalidation() {
    for(bool worldChange:{false,true}) {
        State owner;auto module=std::make_shared<int>(3);std::weak_ptr<int> weak=module;
        initialize(owner,module);auto request=attempt();WorldPackReloadReply reply;
        check(command(owner,WorldPackReloadCommand::Pause,request,reply),"retain module on pause");module.reset();
        check(!weak.expired(),"module pin retained independently of enable caller");
        if(worldChange)owner.observe({123,456,789,112});else check(command(owner,WorldPackReloadCommand::Stop,request,reply),"stop invalidates attempt");
        check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated && owner.snapshot()->reply.oldOwnersRetained,"ambiguous owner/stop never releases old owners");
        check(!command(owner,WorldPackReloadCommand::Resume,request,reply) && !command(owner,WorldPackReloadCommand::ResumeCancelled,request,reply),"invalidated cannot resume");
        check(owner.retainForStop() && !weak.expired(),"invalidated module cannot unload");
        check(!owner.beginDisable(),"disable atomically refuses active/invalidated attempt");
        owner.gpuReceipt(Domain::Icons,owner.snapshot()->serial,OwnedGpuFence::Complete);
        owner.rebuilt(Domain::Projection,owner.snapshot()->serial,true);owner.rebuilt(Domain::Icons,owner.snapshot()->serial,true);
        check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated,"late receipt cannot heal invalidation");
    }
    State owner;auto module=std::make_shared<int>(4);initialize(owner,module);module.reset();
    WorldPackReloadReply reply;check(!command(owner,WorldPackReloadCommand::Pause,attempt(),reply),"missing module ownership prevents pause");
    State disabled;module=std::make_shared<int>(6);initialize(disabled,module);
    check(disabled.beginDisable() && !command(disabled,WorldPackReloadCommand::Pause,attempt(),reply),"teardown closes Pause admission atomically");
}
void teardownRace() {
    for(unsigned i=0;i<64;++i) {
        State owner;auto module=std::make_shared<int>(7);initialize(owner,module);
        std::barrier rendezvous{2};bool paused{},disabled{};
        std::thread thread([&]{rendezvous.arrive_and_wait();WorldPackReloadReply reply;paused=command(owner,WorldPackReloadCommand::Pause,attempt(100+i),reply);});
        rendezvous.arrive_and_wait();disabled=owner.beginDisable();thread.join();
        check(paused!=disabled,"Pause admission and teardown have one atomic winner");
        check(paused ? owner.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated : !owner.snapshot()->enabled,"race cannot expose teardown plus ready participant");
    }
}
void publicationRace() {
    for(unsigned i=0;i<64;++i) {
        State owner;auto module=std::make_shared<int>(8);initialize(owner,module);
        auto lease=owner.enter(Role::MeshJob);std::barrier rendezvous{2};std::atomic_bool pauseReturned{};
        bool committed{},afterPause{};WorldPackReloadReply reply;
        std::thread thread([&]{rendezvous.arrive_and_wait();committed=owner.publishIfCurrent(lease,[&]{afterPause=pauseReturned.load();return true;});});
        rendezvous.arrive_and_wait();check(command(owner,WorldPackReloadCommand::Pause,attempt(1000+i),reply),"publication race pause accepted");
        pauseReturned=true;thread.join();
        check(!committed || !afterPause,"copied publication cannot commit after Pause acceptance");
        check(!owner.publishIfCurrent(lease,[]{return true;}),"post-pause stale worker commit rejected atomically");
    }
}
void cancelledWorkRecovery() {
    State owner;auto module=std::make_shared<int>(9);initialize(owner,module);
    ui::icons::Store icons;auto key=ui::icons::keyFor("minecraft:cancelled",{});
    icons.request(key);auto request=icons.take();
    std::optional<State::Lease> lease{owner.enter(Role::IconDecode)};WorldPackReloadReply reply;
    auto cycle=attempt(2000);check(command(owner,WorldPackReloadCommand::Pause,cycle,reply),"decode cancellation paused");
    bool retried=owner.publishIfCurrent(*lease,[&]{return icons.retry(*request);});
    if(!retried)icons.discard(*request);lease.reset();
    check(!retried && command(owner,WorldPackReloadCommand::CancelBeforeNative,cycle,reply),"old decode retry rejected and drained");
    check(command(owner,WorldPackReloadCommand::ResumeCancelled,cycle,reply),"decode cancellation resumed same epoch");
    icons.request(key);request=icons.take();check(request.has_value(),"discarded working key can be requested again after cancel");
    struct Section {std::uint64_t requestedRevision{4};bool buildInFlight{true},dirty{},incrementalDirty{};};
    std::vector<Section> sections(2);
    projection::detail::CancelledSectionBuild fact{11,12,4,0};
    check(projection::detail::recoverCancelledSection(fact,11,12,sections)
        && !sections[0].buildInFlight && sections[0].dirty && sections[0].incrementalDirty,"actual cancellation helper restores selected section for scheduling");
    check(sections[1].buildInFlight && !sections[1].dirty,"cancel recovery leaves other section untouched");
    sections[0]={};check(!projection::detail::recoverCancelledSection(fact,13,12,sections) && sections[0].buildInFlight,"old worker control fact cannot clear fresh worker in-flight state");
}
void nativeMutationOrdering() {
    State owner;auto module=std::make_shared<int>(10);initialize(owner,module);ui::icons::Store icons;
    auto mutation=owner.beginMutation([&]{icons.beginChange();});WorldPackReloadReply reply;auto cycle=attempt(3000);
    check(command(owner,WorldPackReloadCommand::Pause,cycle,reply) && reply.nativeBorrowers==1,"known native hook origin held across pause");
    check(!command(owner,WorldPackReloadCommand::Prepare,cycle,reply),"prepare cannot pass an observed in-flight origin");
    owner.finishMutation(mutation,[&]{icons.endChange();});
    check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated,"uncoordinated origin crossing Pause invalidates");
    State coordinated;initialize(coordinated,module);cycle=attempt(3001);
    check(command(coordinated,WorldPackReloadCommand::Pause,cycle,reply) && command(coordinated,WorldPackReloadCommand::Prepare,cycle,reply)
        && command(coordinated,WorldPackReloadCommand::BeginNative,cycle,reply),"coordinated native entry fixture");
    auto outer=coordinated.beginMutation([&]{icons.beginChange();});auto inner=coordinated.beginMutation([&]{icons.beginChange();});
    check(!command(coordinated,WorldPackReloadCommand::NativeSettled,cycle,reply) && reply.nativeBorrowers==2,"settlement cannot pass nested observed origins");
    coordinated.finishMutation(inner,[&]{icons.endChange();});check(!icons.readable(),"nested mutation keeps CPU store closed");
    coordinated.finishMutation(outer,[&]{icons.endChange();});
    check(command(coordinated,WorldPackReloadCommand::NativeSettled,cycle,reply) && icons.readable(),"settlement after actual hook-scope drain");
    auto late=coordinated.beginMutation([&]{icons.beginChange();});
    coordinated.finishMutation(late,[&]{icons.endChange();});
    check(coordinated.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated,"late atlas mutation cannot publish fresh Ready");
}
void nativeEngineOwnership() {
    auto module=std::make_shared<int>(11);Owner const physical{123,456,789,111};
    State owner;initialize(owner,module);WorldPackReloadReply reply;auto cycle=attempt(4000);
    check(!owner.nativeResourcesObserved({123,456,789,112}),"native classification requires actual observed physical owner");
    check(owner.nativeResourcesObserved(physical),"normal render owner registers native service lifetime before submission");
    owner.submitted(Domain::Projection);
    check(owner.snapshot()->reply.ownedGpu==OwnedGpuFence::NativeManaged,"native lifetime is distinct from Complete and NoOwnedSubmission");
    std::optional<State::Lease> worker{owner.enter(Role::MeshJob)};
    check(command(owner,WorldPackReloadCommand::Pause,cycle,reply),"native managed owner pauses");
    check(!owner.nativeResourcesObserved(physical),"closed attempt cannot add a lifetime policy");
    check(!command(owner,WorldPackReloadCommand::Prepare,cycle,reply),"native lifetime does not bypass actual CPU job/native borrow drain");
    check(!owner.mayPublish(*worker),"native policy does not permit paused worker publication");worker.reset();
    check(command(owner,WorldPackReloadCommand::Prepare,cycle,reply) && reply.ownedGpu==OwnedGpuFence::NativeManaged,"drained native handles can enter normal game reload without a private FrameBuilder fence claim");
    auto serial=owner.snapshot()->serial;
    check(!owner.gpuReceipt(Domain::Icons,serial,OwnedGpuFence::NativeManaged),"direct D3D icons cannot claim native service ownership");
    check(!owner.gpuReceipt(Domain::Projection,serial,OwnedGpuFence::Complete),"native policy cannot be mislabeled as private GPU Complete");
    check(command(owner,WorldPackReloadCommand::BeginNative,cycle,reply) && command(owner,WorldPackReloadCommand::NativeSettled,cycle,reply),"normal settlement precedes native handle reacquisition");
    serial=owner.snapshot()->serial;
    std::optional<State::Lease> rebuild{owner.enter(Role::Projection,true)};
    owner.retiredOwner(Domain::Projection,serial);
    check(owner.gpuReceipt(Domain::Projection,serial,OwnedGpuFence::NoOwnedSubmission)
        && owner.snapshot()->domainGpu[0]==OwnedGpuFence::NativeManaged,"native destructor does not manufacture no-submission proof");
    owner.rebuilt(Domain::Projection,serial,true);owner.retiredOwner(Domain::Icons,serial);owner.rebuilt(Domain::Icons,serial,true);
    check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Reacquiring,"native reacquire still waits actual owner lease release");rebuild.reset();
    check(command(owner,WorldPackReloadCommand::Query,cycle,reply) && reply.phase==WorldPackReloadPhase::Ready
        && reply.ownedGpu==OwnedGpuFence::NativeManaged && !reply.oldOwnersRetained,"fresh native references ready without claiming first frame GPU completion");
    check(command(owner,WorldPackReloadCommand::Resume,cycle,reply),"native handles resume fresh epoch");
    owner.observe({123,456,789,112});
    check(owner.snapshot()->reply.ownedGpu==OwnedGpuFence::Unknown,"physical owner change invalidates previous classification until normal owner observation");
    check(!owner.nativeResourcesObserved(physical) && owner.nativeResourcesObserved({123,456,789,112}),"new normal owner must register its own exact identity");
    for(auto incomplete:{OwnedGpuFence::Unknown,OwnedGpuFence::Pending}) {
        State direct;initialize(direct,module);direct.submitted(Domain::Projection);
        check(!direct.nativeResourcesObserved(physical),"earlier unknown submission cannot be reclassified as native owned");
        auto request=attempt(4100+static_cast<unsigned>(incomplete));
        check(command(direct,WorldPackReloadCommand::Pause,request,reply),"unregistered projection pauses");
        if(incomplete==OwnedGpuFence::Pending)check(direct.gpuReceipt(Domain::Projection,direct.snapshot()->serial,incomplete),"pending receipt fixture");
        check(!direct.gpuReceipt(Domain::Projection,direct.snapshot()->serial,OwnedGpuFence::NativeManaged)
            && !command(direct,WorldPackReloadCommand::Prepare,request,reply),"Unknown/Pending cannot opt into native policy after submission or Pause");
        State icons;initialize(icons,module);check(icons.nativeResourcesObserved(physical),"native plus direct GPU owner registered");
        icons.submitted(Domain::Icons);request=attempt(4200+static_cast<unsigned>(incomplete));
        check(command(icons,WorldPackReloadCommand::Pause,request,reply),"native plus direct GPU pause");
        if(incomplete==OwnedGpuFence::Pending)check(icons.gpuReceipt(Domain::Icons,icons.snapshot()->serial,incomplete),"private pending receipt fixture");
        check(!command(icons,WorldPackReloadCommand::Prepare,request,reply) && reply.ownedGpu==incomplete,"native ownership never completes Unknown/Pending private icons");
        check(icons.gpuReceipt(Domain::Icons,icons.snapshot()->serial,OwnedGpuFence::Complete)
            && command(icons,WorldPackReloadCommand::Prepare,request,reply) && reply.ownedGpu==OwnedGpuFence::NativeManaged,"actual private icon completion and native policy remain separate");
    }
    auto old=attempt();old.abi=3;reply={};reply.abi=3;
    check(!lholo_resource_reload_v3(WorldPackReloadCommand::Pause,&old,&reply) && reply.error==1,"legacy v3 export cannot admit a v4 policy");
    State legacy;initialize(legacy,module);reply={};
    check(!legacy.call(WorldPackReloadCommand::Pause,&old,&reply),"v4 export rejects v3 request ABI");
}
void rendererRetarget() {
    auto module=std::make_shared<int>(12);WorldPackReloadReply reply;
    {
        State owner;initialize(owner,module);auto cycle=attempt(4300),updated=cycle;updated.renderer=790;
        check(command(owner,WorldPackReloadCommand::Pause,cycle,reply),"renderer fixture pauses old owner");
        check(!command(owner,WorldPackReloadCommand::NativeSettled,updated,reply),"pre-native renderer retarget rejected");
        check(command(owner,WorldPackReloadCommand::Prepare,cycle,reply),"renderer fixture prepares old owner");
        owner.observe({123,456,790,111});
        check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated,"physical renderer change in Prepared invalidates");
    }
    {
        State owner;initialize(owner,module);auto cycle=attempt(4301),updated=cycle;updated.renderer=790;
        check(owner.nativeResourcesObserved({123,456,789,111}),"native-managed renderer fixture registers owner");
        owner.submitted(Domain::Projection);
        check(command(owner,WorldPackReloadCommand::Pause,cycle,reply)
            &&command(owner,WorldPackReloadCommand::Prepare,cycle,reply)
            &&command(owner,WorldPackReloadCommand::BeginNative,cycle,reply),"native renderer replacement enters active phase");
        auto mutation=owner.beginMutation([]{});
        owner.observe({123,456,790,111});
        check(owner.snapshot()->reply.phase==WorldPackReloadPhase::NativeActive
            &&owner.snapshot()->reply.renderer==789,"physical replacement remains pending until settlement");
        for(auto bad:{[&]{auto v=updated;v.token++;return v;}(),
                      [&]{auto v=updated;v.client++;return v;}(),
                      [&]{auto v=updated;v.world++;return v;}(),
                      [&]{auto v=updated;v.oldEpoch++;return v;}(),
                      [&]{auto v=updated;v.nextEpoch++;return v; }()})
            check(!command(owner,WorldPackReloadCommand::NativeSettled,bad,reply),"foreign attempt cannot retarget renderer");
        check(!command(owner,WorldPackReloadCommand::NativeSettled,cycle,reply),"old renderer cannot settle while new physical owner pending");
        check(!command(owner,WorldPackReloadCommand::NativeSettled,updated,reply)
            &&reply.nativeBorrowers,"native mutation borrower prevents retarget commit");
        owner.finishMutation(mutation,[]{});
        check(command(owner,WorldPackReloadCommand::NativeSettled,updated,reply)
            &&reply.renderer==790&&reply.epoch==2&&reply.ownedGpu==OwnedGpuFence::NativeManaged,
            "drained active replacement retargets and preserves native-managed policy");
        check(!command(owner,WorldPackReloadCommand::Query,cycle,reply),"old renderer Query rejected after retarget");
        auto serial=owner.snapshot()->serial;
        owner.retiredOwner(Domain::Projection,serial);owner.rebuilt(Domain::Projection,serial,true);
        owner.retiredOwner(Domain::Icons,serial);owner.rebuilt(Domain::Icons,serial,true);
        check(command(owner,WorldPackReloadCommand::Query,updated,reply)
            &&reply.phase==WorldPackReloadPhase::Ready&&reply.renderer==790,"new renderer Ready has exact request identity");
        check(!command(owner,WorldPackReloadCommand::Resume,cycle,reply)
            &&command(owner,WorldPackReloadCommand::Resume,updated,reply),"only new renderer resumes after Ready");
    }
    {
        State owner;initialize(owner,module);auto cycle=attempt(4302);
        check(command(owner,WorldPackReloadCommand::Pause,cycle,reply)
            &&command(owner,WorldPackReloadCommand::Prepare,cycle,reply)
            &&command(owner,WorldPackReloadCommand::BeginNative,cycle,reply),"foreign physical owner fixture active");
        owner.observe({123,456,790,112});
        check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated,"dimension change remains invalidated even with renderer replacement");
    }
}
// Physical identities must be rebound before submission, never after unknown work.
void nativeOwnerClassificationRegression() {
    Owner const first{123,456,789,111},second{124,456,789,112};
    auto module=std::make_shared<int>(6);
    for(auto incomplete:{OwnedGpuFence::Unknown,OwnedGpuFence::Pending}) {
        for(bool submitAgain:{false,true}) {
            State owner;initialize(owner,module);
            check(owner.nativeResourcesObserved(first),"first physical owner registered");
            owner.observe(second);owner.submitted(Domain::Projection);
            check(owner.snapshot()->domainGpu[0]==OwnedGpuFence::Unknown,"new owner submission remains unclassified");
            WorldPackReloadReply reply;auto request=attempt(4300+static_cast<unsigned>(incomplete));
            if(incomplete==OwnedGpuFence::Pending) {
                check(command(owner,WorldPackReloadCommand::Pause,request,reply),"changed owner pending fixture pauses");
                check(owner.gpuReceipt(Domain::Projection,owner.snapshot()->serial,incomplete),"changed owner pending fixture receipt");
                check(command(owner,WorldPackReloadCommand::CancelBeforeNative,request,reply)
                    && command(owner,WorldPackReloadCommand::ResumeCancelled,request,reply),"pending fixture returns without inventing completion");
                request=attempt(4400+static_cast<unsigned>(incomplete));
            }
            owner.observe(first);
            check(!owner.nativeResourcesObserved(first),"returning physical owner cannot bless earlier incomplete work");
            if(submitAgain) {
                owner.submitted(Domain::Projection);
                check(owner.snapshot()->domainGpu[0]!=OwnedGpuFence::NativeManaged,"stale registration cannot reclassify another submission");
            }
            check(command(owner,WorldPackReloadCommand::Pause,request,reply),"returning owner closes admission");
            check(!owner.gpuReceipt(Domain::Projection,owner.snapshot()->serial,OwnedGpuFence::NativeManaged)
                && !command(owner,WorldPackReloadCommand::Prepare,request,reply),"Unknown/Pending cannot receive stale native policy");
        }
    }
    State rebound;initialize(rebound,module);check(rebound.nativeResourcesObserved(first),"valid first owner classification");
    rebound.observe(second);
    check(rebound.nativeResourcesObserved(second),"new physical owner may register before its first submission");
    rebound.submitted(Domain::Projection);
    check(rebound.snapshot()->domainGpu[0]==OwnedGpuFence::NativeManaged,"new pre-submission policy remains supported");
    rebound.invalidate();rebound.observe(first);rebound.submitted(Domain::Projection);
    check(rebound.snapshot()->domainGpu[0]==OwnedGpuFence::Unknown,"open invalidation clears prior physical owner binding");
}

void ownerCallbackLockRegression() {
    auto module=std::make_shared<int>(7);
    for(int scenario=0;scenario<5;++scenario) {
        State owner;initialize(owner,module);WorldPackReloadReply reply;auto request=attempt(4500+scenario);
        check(command(owner,WorldPackReloadCommand::Pause,request,reply)
            && command(owner,WorldPackReloadCommand::Prepare,request,reply)
            && command(owner,WorldPackReloadCommand::BeginNative,request,reply)
            && command(owner,WorldPackReloadCommand::NativeSettled,request,reply),"lock callback fixture enters reacquisition");
        std::mutex worldLock;std::unique_lock teardown(worldLock);
        std::promise<void> admitted;auto admittedFuture=admitted.get_future();
        std::atomic_bool exit{},destroyed{};std::atomic_int nativeTouches{};
        auto callback=std::async(std::launch::async,[&] {
            auto checkpoint=owner.snapshot();
            auto lease=owner.enter(Role::Projection,true);bool const valid=bool(lease);
            admitted.set_value();if(!valid)return false;
            std::lock_guard lifetime(worldLock);
            auto current=owner.ownerCallbackSnapshot(lease,*checkpoint,WorldPackReloadPhase::Reacquiring,exit.load(),destroyed.load());
            if(!current || current->ready[0])return false;
            ++nativeTouches;return true;
        });
        admittedFuture.wait();
        if(scenario==0)owner.invalidate();
        if(scenario==1)check(command(owner,WorldPackReloadCommand::Pause,attempt(4600,2,3),reply),"queued callback superseded by a new generation");
        if(scenario==2)exit=true;
        if(scenario==3)destroyed=true;
        if(scenario==4) {
            auto const serial=owner.snapshot()->serial;
            owner.retiredOwner(Domain::Projection,serial);owner.rebuilt(Domain::Projection,serial,true);
        }
        teardown.unlock();
        check(!callback.get() && nativeTouches==0,"callback revalidates generation/world/readiness after its lifetime lock wait");
        check(owner.snapshot()->reply.cpuReaders==0 && owner.snapshot()->reply.nativeBorrowers==0,"rejected callback releases its real owner lease");
    }
    State current;initialize(current,module);WorldPackReloadReply reply;auto request=attempt(4700);
    check(command(current,WorldPackReloadCommand::Pause,request,reply)
        && command(current,WorldPackReloadCommand::Prepare,request,reply)
        && command(current,WorldPackReloadCommand::BeginNative,request,reply)
        && command(current,WorldPackReloadCommand::NativeSettled,request,reply),"current callback fixture enters reacquisition");
    auto checkpoint=current.snapshot();auto serial=checkpoint->serial;
    current.retiredOwner(Domain::Projection,serial);current.rebuilt(Domain::Projection,serial,true);
    current.retiredOwner(Domain::Icons,serial);current.rebuilt(Domain::Icons,serial,true);
    check(command(current,WorldPackReloadCommand::Resume,request,reply),"pre-admission gap fixture finishes its first generation");
    request=attempt(4701,2,3);
    check(command(current,WorldPackReloadCommand::Pause,request,reply)
        && command(current,WorldPackReloadCommand::Prepare,request,reply)
        && command(current,WorldPackReloadCommand::BeginNative,request,reply)
        && command(current,WorldPackReloadCommand::NativeSettled,request,reply),"pre-admission gap fixture enters a new generation");
    auto latest=current.snapshot();auto lease=current.enter(Role::Projection,true);
    check(!current.ownerCallbackSnapshot(lease,*checkpoint,WorldPackReloadPhase::Reacquiring,false,false),"old checkpoint cannot acquire a new generation's callback lease");
    check(bool(current.ownerCallbackSnapshot(lease,*latest,WorldPackReloadPhase::Reacquiring,false,false)),"current live callback remains admitted");
    check(!current.ownerCallbackSnapshot(lease,*latest,WorldPackReloadPhase::Pausing,false,false),"callback expected phase is checked");
    State other;initialize(other,module);auto foreign=other.enter(Role::Projection);
    check(!current.ownerCallbackSnapshot(foreign,*latest,WorldPackReloadPhase::Reacquiring,false,false),"foreign participant lease cannot authorize native owner access");
}

// Native catalog/decode is intentionally replaced by owned synthetic pixels
// only in this fixture. Production GameTick is not claimed as exercised here.
namespace lholo::ui::icons {
std::uint64_t fixtureSerial{};std::vector<Key> fixtureKeys;
bool fixturePermit{true};
void startResourceReload(std::uint64_t epoch,std::uint64_t serial,std::vector<Key> keys) {
    store().resetResourceEpoch(epoch);fixtureSerial=serial;fixtureKeys=std::move(keys);
}
bool resourceReloadReady(std::uint64_t serial) {
    if(serial!=fixtureSerial || !fixturePermit)return false;
    auto lease=resourcepacks::state().enter(Role::IconDecode,true);if(!lease)return false;
    for(auto const& key:fixtureKeys)store().queueRebuild(key);
    while(auto request=store().take()) {
        auto pixels=std::make_shared<Pixels>();pixels->width=2;pixels->height=2;pixels->rgba.assign(16,227);
        if(resourcepacks::state().mayPublish(lease))store().publish(*request,std::move(pixels));
    }
    return store().drained();
}
}
void warp(bool on12) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    // Retain the actual interop device and its direct queue throughout the test.
    // This is our private fixture queue, not a captured native FrameBuilder queue.
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device12;ComPtr<ID3D12CommandQueue> queue;
    if(on12) {
        check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"real DXGI WARP adapter factory");
        if(!factory)return;
        check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))),"real D3D12 WARP adapter");
        if(!adapter)return;
        check(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device12))),"real WARP D3D12 device");
        if(!device12)return;
        D3D12_COMMAND_QUEUE_DESC queueDescription{};queueDescription.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(SUCCEEDED(device12->CreateCommandQueue(&queueDescription,IID_PPV_ARGS(&queue))),"real private D3D12 direct queue");
        if(!queue)return;
        IUnknown* queues[]{queue.Get()};
        check(SUCCEEDED(D3D11On12CreateDevice(device12.Get(),0,nullptr,0,queues,1,0,&device,&context,nullptr)),"real WARP D3D11On12 owner context");
    } else {
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"real WARP D3D11 owner context");
    }
    std::printf("Owned icon GPU fixture backend: %s\n",on12?"D3D11On12 / D3D12 WARP / private direct queue":"D3D11 WARP");
    if(!device)return;
    auto& owner=resourcepacks::state();auto module=std::make_shared<int>(5);initialize(owner,module);
    overlay::BlockIconGpu icons;GpuOwner pump;auto& store=ui::icons::store();store.resetResourceEpoch(1);
    auto key=ui::icons::keyFor("minecraft:fixture",{});store.request(key);auto request=store.take();
    auto pixels=std::make_shared<ui::icons::Pixels>();pixels->width=2;pixels->height=2;pixels->rgba.assign(16,31);
    check(request && store.publish(*request,pixels),"old epoch copied pixels");icons.beginFrame(device.Get());auto view=icons.lookup(key.block,key.item);
    check(view.texture && icons.cached()==1,"old private SRV created");
    auto* srv=reinterpret_cast<ID3D11ShaderResourceView*>(view.texture);
    ComPtr<ID3D11Resource> source;srv->GetResource(&source);ComPtr<ID3D11Texture2D> texture;source.As(&texture);
    D3D11_TEXTURE2D_DESC description;texture->GetDesc(&description);description.Usage=D3D11_USAGE_STAGING;description.BindFlags=0;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;check(SUCCEEDED(device->CreateTexture2D(&description,nullptr,&staging)),"owned GPU submission fixture destination");
    context->CopyResource(staging.Get(),source.Get());icons.endFrame();source.Reset();texture.Reset();
    WorldPackReloadReply reply;auto cycle=attempt(500);
    check(command(owner,WorldPackReloadCommand::Pause,cycle,reply),"real icon participant paused");
    check(!icons.lookup(key.block,key.item).texture,"closed UI cannot publish old view");
    check(!command(owner,WorldPackReloadCommand::Prepare,cycle,reply) && icons.cached()==1,"SRV retained before actual event receipt");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{3};
    do {pump.pump(owner,icons,device.Get(),context.Get());if(owner.snapshot()->domainGpu[1]==OwnedGpuFence::Complete)break;std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);
    check(owner.snapshot()->domainGpu[1]==OwnedGpuFence::Complete && icons.cached()==1,"real old GPU event completed while SRV owner retained");
    // GetData must cover actual submitted work, not only an otherwise idle EVENT.
    // Use DONOTWAIT after the receipt, so a failed receipt cannot be hidden by Map.
    D3D11_MAPPED_SUBRESOURCE mapped{};
    auto const mapResult=context->Map(staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
    check(SUCCEEDED(mapResult),"completed old EVENT permits nonblocking GPU copy readback");
    if(SUCCEEDED(mapResult)) {
        bool copied=static_cast<unsigned char const*>(mapped.pData)[0]==31;
        for(unsigned row=0;row<2;++row)for(unsigned col=0;col<8;++col)
            copied=copied && static_cast<unsigned char const*>(mapped.pData)[row*mapped.RowPitch+col]==31;
        check(copied,"old EVENT covers the private icon texture GPU copy");
        context->Unmap(staging.Get(),0);
    }
    check(command(owner,WorldPackReloadCommand::Prepare,cycle,reply),"actual icon GPU receipt permits no-native-mesh participant");
    check(command(owner,WorldPackReloadCommand::BeginNative,cycle,reply) && icons.cached()==1,"old icon ownership retained through native interval");
    check(command(owner,WorldPackReloadCommand::NativeSettled,cycle,reply),"synthetic native settlement notification");
    auto serial=owner.snapshot()->serial;owner.retiredOwner(Domain::Projection,serial);owner.rebuilt(Domain::Projection,serial,true);
    ui::icons::fixturePermit=false;pump.pump(owner,icons,device.Get(),context.Get());
    check(icons.cached()==0 && owner.snapshot()->reply.phase==WorldPackReloadPhase::Reacquiring,"fresh metadata pending keeps admission closed after safe retirement");
    auto rollback=attempt(501,2,3);
    check(command(owner,WorldPackReloadCommand::Pause,rollback,reply),"actual icon rebuild rollback starts a new token");
    check(command(owner,WorldPackReloadCommand::Prepare,rollback,reply),"retired no-owned-work rollback prepared");
    check(command(owner,WorldPackReloadCommand::BeginNative,rollback,reply),"rollback synthetic native entry");
    check(command(owner,WorldPackReloadCommand::NativeSettled,rollback,reply),"rollback synthetic settlement advances again");
    serial=owner.snapshot()->serial;owner.retiredOwner(Domain::Projection,serial);owner.rebuilt(Domain::Projection,serial,true);
    ui::icons::fixturePermit=true;
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds{3};
    do {pump.pump(owner,icons,device.Get(),context.Get());if(owner.snapshot()->reply.phase==WorldPackReloadPhase::Ready)break;std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);
    check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Ready && icons.cached()==1,"fresh owned SRV plus real new GPU query becomes Ready");
    check(store.request(key).pixels && store.request(key).pixels->rgba[0]==227,"fixture rebuilt fresh resource epoch pixels");
    check(!store.publish(*request,pixels),"old resource epoch cannot republish");
    check(!command(owner,WorldPackReloadCommand::Resume,cycle,reply),"original cycle cannot resume rollback");
    check(command(owner,WorldPackReloadCommand::Resume,rollback,reply),"real icon owner resumes after fresh rollback rebuild");pump.pump(owner,icons,device.Get(),context.Get());
    icons.beginFrame(device.Get());check(icons.lookup(key.block,key.item).texture!=0,"new epoch normal UI view");icons.endFrame();
    cycle=attempt(502,3,4);owner.submitted(Domain::Projection);
    check(command(owner,WorldPackReloadCommand::Pause,cycle,reply),"native-unknown test paused");
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds{3};
    do {pump.pump(owner,icons,device.Get(),context.Get());if(owner.snapshot()->domainGpu[1]==OwnedGpuFence::Complete)break;std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);
    check(!command(owner,WorldPackReloadCommand::Prepare,cycle,reply) && reply.ownedGpu==OwnedGpuFence::Unknown,"real icon GPU completion never substitutes native mesh fence");
    ComPtr<ID3D11Device> other;ComPtr<ID3D11DeviceContext> otherContext;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&other,nullptr,&otherContext)),"replacement device fixture");
    pump.pump(owner,icons,other.Get(),otherContext.Get());
    check(owner.snapshot()->reply.phase==WorldPackReloadPhase::Invalidated && icons.cached()==1,"device owner change invalidates and retains old views");
    check(command(owner,WorldPackReloadCommand::Stop,cycle,reply),"stop while native fence unknown");icons.reset();
    check(icons.cached()==1 && owner.snapshot()->reply.oldOwnersRetained,"invalidated reset retains icon owners");
    std::printf("Native engine reload / GameTick catalog decode: NOT_RUN. Unregistered native submission stays Unknown; native service policy is separate from GPU Complete.\n");
}
int main(int argc,char** argv){
    bool const on12=argc==2 && std::strcmp(argv[1],"--on12")==0;
    if(argc>1 && !on12){std::printf("Usage: LHoloWorldPackReloadTests [--on12]\n");return 2;}
    protocol();cancellationAndGpu();invalidation();teardownRace();publicationRace();cancelledWorkRecovery();nativeMutationOrdering();nativeEngineOwnership();rendererRetarget();nativeOwnerClassificationRegression();ownerCallbackLockRegression();warp(on12);
    std::printf("World pack v4 checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
