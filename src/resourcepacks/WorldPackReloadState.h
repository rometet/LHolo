#pragma once
#include "resourcepacks/WorldPackReloadApi.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <utility>

namespace lholo::resourcepacks {
using namespace praxis::companion::packs;
static_assert(sizeof(WorldPackReloadRequest)==56 && sizeof(WorldPackReloadReply)==88);
enum class Role {Projection, MeshJob, IconDecode, IconRender, NativeMutation};
enum class Domain {Projection, Icons};
enum class Error : std::uint32_t {None, Abi, Owner, Token, Epoch, Phase, Busy, GpuUnknown,
    Disabled, Module, Invalidated, Reacquire};
// Private physical identities. `level` is NOT the coordinator's world stamp.
struct Owner {std::uint64_t level{},client{},renderer{},dimension{};auto operator<=>(Owner const&) const=default;};
struct Snapshot {
    WorldPackReloadReply reply;
    std::uint64_t serial{};
    std::uint64_t oldEpoch{},nextEpoch{};
    std::array<bool,2> ready{},retired{};
    std::array<OwnedGpuFence,2> domainGpu{};
    bool enabled{},closed{};
};
class State {
public:
    class Lease {
        State* state{};Role role{};
        friend class State;
        Lease(State* s,Role r,std::uint64_t e,std::uint64_t generation):state(s),role(r),epoch(e),serial(generation){}
    public:
        std::uint64_t epoch{},serial{};
        Lease()=default;
        Lease(Lease const&)=delete;
        Lease& operator=(Lease const&)=delete;
        Lease(Lease&& other) noexcept:state(std::exchange(other.state,nullptr)),role(other.role),epoch(other.epoch),serial(other.serial){}
        ~Lease(){if(state)state->release(role);}
        explicit operator bool() const noexcept{return state!=nullptr;}
    };
    State():emergency(std::make_shared<Snapshot>()) {
        emergency->reply.phase=WorldPackReloadPhase::Invalidated;
        emergency->reply.admissionClosed=1;emergency->reply.oldOwnersRetained=1;
        emergency->reply.error=static_cast<std::uint32_t>(Error::Busy);emergency->closed=true;
        publish();
    }
    std::shared_ptr<Snapshot const> snapshot() const noexcept{return published.load(std::memory_order_acquire);}
    // Call after acquiring the session and world lifetime locks. An admitted
    // callback may have waited while its attempt or physical world was retired.
    std::shared_ptr<Snapshot const> ownerCallbackSnapshot(Lease const& lease,Snapshot const& checkpoint,
        WorldPackReloadPhase expected,bool worldExit,bool sourceDestroyed) const noexcept {
        auto current=snapshot();
        if(lease.state!=this || !current->enabled || !current->closed
            || checkpoint.serial!=lease.serial || checkpoint.reply.epoch!=lease.epoch
            || current->serial!=lease.serial || current->reply.epoch!=lease.epoch
            || current->reply.phase!=expected || worldExit || sourceDestroyed)return {};
        return current;
    }
    bool enable(std::shared_ptr<void> const& owner) {
        std::lock_guard lock(mutex);
        if(closed || !owner)return false;
        module=owner;enabled=true;phase=WorldPackReloadPhase::Idle;publish();return true;
    }
    Lease enter(Role role,bool rebuilding=false) {
        std::lock_guard lock(mutex);
        if(closed && !(rebuilding && phase==WorldPackReloadPhase::Reacquiring))return {};
        adjust(role,1);publish();
        if(phase==WorldPackReloadPhase::Invalidated){adjust(role,-1);return {};}
        return {this,role,epoch,serial};
    }
    bool mayPublish(Lease const& lease) {
        std::lock_guard lock(mutex);
        bool const ok=lease && lease.epoch==epoch && lease.serial==serial
            && (!closed || phase==WorldPackReloadPhase::Reacquiring);
        if(!ok){++rejected;publish();}return ok;
    }
    // Short copied-value publication only. No native/GPU calls, drains or
    // callbacks into State are permitted inside this operation. Admission and
    // publication share the same lock, so Pause cannot slip between the epoch
    // check and a worker/store queue mutation.
    template<class Publish> bool publishIfCurrent(Lease const& lease,Publish&& operation) {
        std::lock_guard lock(mutex);
        if(!lease || lease.epoch!=epoch || lease.serial!=serial
            || (closed && phase!=WorldPackReloadPhase::Reacquiring)) {
            ++rejected;publish();return false;
        }
        return static_cast<bool>(std::forward<Publish>(operation)());
    }
    template<class BeginCpu> Lease beginMutation(BeginCpu&& operation) {
        std::lock_guard lock(mutex);
        if(closed && phase!=WorldPackReloadPhase::NativeActive)invalidateLocked(Error::Invalidated);
        std::forward<BeginCpu>(operation)();
        adjust(Role::NativeMutation,1);publish();return {this,Role::NativeMutation,epoch,serial};
    }
    template<class EndCpu> void finishMutation(Lease& lease,EndCpu&& operation) {
        std::lock_guard lock(mutex);std::forward<EndCpu>(operation)();
        if(lease.state!=this)return;
        if(closed && (phase!=WorldPackReloadPhase::NativeActive || lease.serial!=serial || lease.epoch!=epoch))
            invalidateLocked(Error::Invalidated);
        lease.state=nullptr;adjust(Role::NativeMutation,-1);advance();publish();
    }
    void observe(Owner value) {
        std::lock_guard lock(mutex);
        if(closed){
            if(phase==WorldPackReloadPhase::NativeActive){
                bool samePlace=value.level==attemptOwner.level&&value.client==attemptOwner.client
                    &&value.dimension==attemptOwner.dimension&&value.renderer;
                if(!samePlace || (pendingOwner.renderer && value!=pendingOwner)){
                    invalidateLocked(Error::Owner);publish();return;
                }
                if(value!=attemptOwner)pendingOwner=value;
            }else if(value!=attemptOwner){invalidateLocked(Error::Owner);publish();return;}
        }
        if(!closed && value!=owner){request={};nativeOwner={};nativeManaged=false;pendingOwner={};}
        if(!closed)owner=value;
        if(!closed){request.client=value.client;request.renderer=value.renderer;}
        publish();
    }
    void invalidate() {
        std::lock_guard lock(mutex);
        if(closed)invalidateLocked(Error::Invalidated);
        else {owner={};request={};nativeOwner={};pendingOwner={};nativeManaged=false;}
        publish();
    }
    bool retainForStop() {
        std::lock_guard lock(mutex);
        if(!closed)return false;
        if(!modulePin)modulePin=module.lock();
        invalidateLocked(Error::Invalidated);publish();return true;
    }
    bool beginDisable() {
        std::lock_guard lock(mutex);
        if(closed) {
            if(!modulePin)modulePin=module.lock();
            invalidateLocked(Error::Invalidated);publish();return false;
        }
        // Atomic with Pause admission: teardown cannot race a newly accepted
        // attempt after a separate 'not paused' check.
        enabled=false;publish();return true;
    }
    void disableInactive() {
        std::lock_guard lock(mutex);if(closed)return;
        enabled=false;module.reset();owner={};request={};nativeOwner={};pendingOwner={};nativeManaged=false;
        gpu={OwnedGpuFence::NoOwnedSubmission,OwnedGpuFence::NoOwnedSubmission};publish();
    }
    // Called only at the normal BaseActorRenderContext owner, before native
    // mce::Mesh submission. It cannot bless earlier Unknown/Pending work or an
    // already closed attempt. Native service handles retain their own lifetime;
    // the participant drains CPU borrowers and reacquires them after settlement.
    bool nativeResourcesObserved(Owner value) {
        std::lock_guard lock(mutex);
        if(!enabled || closed || phase!=WorldPackReloadPhase::Idle || value!=owner
            || !value.level || !value.client || !value.renderer || !value.dimension
            || (gpu[0]!=OwnedGpuFence::NoOwnedSubmission && gpu[0]!=OwnedGpuFence::NativeManaged))return false;
        nativeOwner=value;nativeManaged=true;gpu[0]=OwnedGpuFence::NativeManaged;publish();return true;
    }
    void submitted(Domain domain) {
        std::lock_guard lock(mutex);
        auto const i=index(domain);
        // Only a pre-submission classification can survive submission. An old
        // binding cannot turn earlier Unknown/Pending work into native policy.
        gpu[i]=domain==Domain::Projection && nativeManaged && nativeOwner==owner
            && (gpu[i]==OwnedGpuFence::NoOwnedSubmission || gpu[i]==OwnedGpuFence::NativeManaged)
            ?OwnedGpuFence::NativeManaged:OwnedGpuFence::Unknown;
        publish();
    }
    bool gpuReceipt(Domain domain,std::uint64_t ticket,OwnedGpuFence result) {
        std::lock_guard lock(mutex);
        if(ticket!=serial || !closed || phase==WorldPackReloadPhase::Invalidated)return false;
        if(result!=OwnedGpuFence::Unknown && result!=OwnedGpuFence::Pending
            && result!=OwnedGpuFence::Complete && result!=OwnedGpuFence::NoOwnedSubmission
            && result!=OwnedGpuFence::NativeManaged)return false;
        if(result==OwnedGpuFence::NativeManaged
            && (domain!=Domain::Projection || !nativeManaged || nativeOwner!=owner
                || gpu[0]!=OwnedGpuFence::NativeManaged))return false;
        // Releasing a native client handle does not claim its submitted frame
        // has completed. Preserve the explicit engine-managed lifetime policy.
        if(domain==Domain::Projection && nativeManaged && nativeOwner==owner
            && gpu[0]==OwnedGpuFence::NativeManaged) {
            if(result!=OwnedGpuFence::NativeManaged && result!=OwnedGpuFence::NoOwnedSubmission)return false;
            result=OwnedGpuFence::NativeManaged;
        }
        gpu[index(domain)]=result;advance();publish();return true;
    }
    void retiredOwner(Domain domain,std::uint64_t ticket) {
        std::lock_guard lock(mutex);
        if(ticket!=serial || phase!=WorldPackReloadPhase::Reacquiring)return;
        retired[index(domain)]=true;publish();
    }
    void rebuilt(Domain domain,std::uint64_t ticket,bool success) {
        std::lock_guard lock(mutex);
        if(ticket!=serial || phase!=WorldPackReloadPhase::Reacquiring)return;
        auto const i=index(domain);ready[i]=success && retired[i];
        if(!ready[i])error=Error::Reacquire;
        advance();publish();
    }
    int call(WorldPackReloadCommand command,WorldPackReloadRequest const* input,WorldPackReloadReply* output) noexcept {
        if(!output || output->bytes!=sizeof(WorldPackReloadReply))return 0;
        if(!input || input->bytes!=sizeof(WorldPackReloadRequest) || input->abi!=WorldPackReloadAbi
            || output->abi!=WorldPackReloadAbi) {
            *output={};output->error=static_cast<std::uint32_t>(Error::Abi);return 0;
        }
        auto const value=*input;
        if(command==WorldPackReloadCommand::Query) {
            // Atomic copied snapshot only: no mutex/drain/native/GPU operation.
            auto current=snapshot();*output=current->reply;
            bool const bootstrap=value.token==0 && phaseOf(*current)==WorldPackReloadPhase::Idle
                && value.world==0 && value.client==0 && value.renderer==0 && value.oldEpoch==0 && value.nextEpoch==0;
            bool const match=value.token==output->token && value.world==output->world
                && value.client==output->client && value.renderer==output->renderer
                && value.oldEpoch==current->oldEpoch && value.nextEpoch==current->nextEpoch;
            if(!current->enabled || (!bootstrap && !match)){output->error=static_cast<std::uint32_t>(Error::Token);return 0;}
            return 1;
        }
        try {
            std::lock_guard lock(mutex);
            bool accepted=false;Error rejectedError=Error::Phase;
            if(!enabled)rejectedError=Error::Disabled;
            else if(command==WorldPackReloadCommand::Pause && !same(value)
                && (phase==WorldPackReloadPhase::Idle || phase==WorldPackReloadPhase::Ready
                    || phase==WorldPackReloadPhase::Reacquiring)) {
                if(!value.world || value.client!=owner.client || value.renderer!=owner.renderer
                    || !owner.level || !owner.dimension || !owner.client || !owner.renderer
                    || (closed && (value.world!=request.world || owner!=attemptOwner)))rejectedError=Error::Owner;
                else if(!value.token || tokens.contains(value.token) || tokens.size()>=4096)rejectedError=Error::Token;
                else if(value.oldEpoch!=epoch || value.nextEpoch<=epoch)rejectedError=Error::Epoch;
                else if(auto pin=module.lock()) {
                    modulePin=std::move(pin);tokens.insert(value.token);request=value;attemptOwner=owner;pendingOwner={};++serial;
                    closed=true;retained=true;cancel=false;ready={};retired={};
                    phase=WorldPackReloadPhase::Pausing;error=Error::None;accepted=true;
                } else rejectedError=Error::Module;
            } else if(!same(value)&&!canRetarget(command,value))rejectedError=Error::Token;
            else if(phase==WorldPackReloadPhase::Invalidated) {
                accepted=command==WorldPackReloadCommand::Stop;rejectedError=Error::Invalidated;
            } else {
                switch(command) {
                case WorldPackReloadCommand::Pause:accepted=phase==WorldPackReloadPhase::Pausing;break;
                case WorldPackReloadCommand::Prepare:
                    if(phase==WorldPackReloadPhase::Prepared)accepted=true;
                    else if(phase==WorldPackReloadPhase::Pausing && drained() && gpuDone()) {
                        phase=WorldPackReloadPhase::Prepared;accepted=true;
                    } else rejectedError=drained()?Error::GpuUnknown:Error::Busy;
                    break;
                case WorldPackReloadCommand::BeginNative:
                    if(phase==WorldPackReloadPhase::Prepared || phase==WorldPackReloadPhase::NativeActive) {
                        phase=WorldPackReloadPhase::NativeActive;accepted=true;
                    }break;
                case WorldPackReloadCommand::NativeSettled:
                    if(phase==WorldPackReloadPhase::NativeActive && drained()) {
                        if(pendingOwner.renderer && value.renderer!=pendingOwner.renderer){rejectedError=Error::Owner;break;}
                        if(pendingOwner.renderer){
                            if(nativeManaged&&nativeOwner==attemptOwner)nativeOwner=pendingOwner;
                            owner=attemptOwner=pendingOwner;request.renderer=pendingOwner.renderer;pendingOwner={};
                        }
                        epoch=request.nextEpoch;++serial;phase=WorldPackReloadPhase::Reacquiring;accepted=true;
                    } else {
                        accepted=phase==WorldPackReloadPhase::Reacquiring || phase==WorldPackReloadPhase::Ready;
                        if(phase==WorldPackReloadPhase::NativeActive)rejectedError=Error::Busy;
                    }
                    break;
                case WorldPackReloadCommand::Resume:
                    if(phase==WorldPackReloadPhase::Ready) {
                        phase=WorldPackReloadPhase::Idle;closed=false;retained=false;modulePin.reset();lastCompletion=command;accepted=true;
                    } else accepted=phase==WorldPackReloadPhase::Idle && lastCompletion==command;
                    break;
                case WorldPackReloadCommand::CancelBeforeNative:
                    if(phase==WorldPackReloadPhase::Pausing || phase==WorldPackReloadPhase::Prepared
                        || phase==WorldPackReloadPhase::Cancelled) {cancel=true;advance();accepted=true;}
                    break;
                case WorldPackReloadCommand::ResumeCancelled:
                    if(phase==WorldPackReloadPhase::Cancelled) {
                        phase=WorldPackReloadPhase::Idle;closed=false;retained=false;cancel=false;
                        modulePin.reset();++serial;lastCompletion=command;accepted=true;
                    } else accepted=phase==WorldPackReloadPhase::Idle && lastCompletion==command;
                    break;
                case WorldPackReloadCommand::Stop:invalidateLocked(Error::Invalidated);accepted=true;break;
                default:break;
                }
            }
            publish();*output=published.load()->reply;
            if(!accepted)output->error=static_cast<std::uint32_t>(rejectedError);
            return accepted?1:0;
        } catch(...) {*output={};output->error=static_cast<std::uint32_t>(Error::Busy);return 0;}
    }
private:
    static std::size_t index(Domain d){return d==Domain::Projection?0:1;}
    static WorldPackReloadPhase phaseOf(Snapshot const& s){return s.reply.phase;}
    bool same(WorldPackReloadRequest const& v) const {
        return v.token==request.token && v.world==request.world && v.client==request.client && v.renderer==request.renderer
            && v.oldEpoch==request.oldEpoch && v.nextEpoch==request.nextEpoch;
    }
    bool canRetarget(WorldPackReloadCommand command,WorldPackReloadRequest const& v) const noexcept {
        return command==WorldPackReloadCommand::NativeSettled&&phase==WorldPackReloadPhase::NativeActive&&drained()
            &&pendingOwner.renderer&&v.renderer==pendingOwner.renderer&&v.token==request.token
            &&v.world==request.world&&v.client==request.client&&v.oldEpoch==request.oldEpoch
            &&v.nextEpoch==request.nextEpoch;
    }
    bool drained() const{return readers==0 && jobs==0 && borrowers==0;}
    bool gpuDone() const {
        for(std::size_t i=0;i<gpu.size();++i) {
            auto const g=gpu[i];
            if(g==OwnedGpuFence::NativeManaged) {
                if(i!=0 || !nativeManaged || nativeOwner!=owner)return false;
            } else if(g!=OwnedGpuFence::Complete && g!=OwnedGpuFence::NoOwnedSubmission)return false;
        }
        return true;
    }
    void adjust(Role role,int delta) {
        if(role==Role::MeshJob || role==Role::IconDecode)jobs+=delta;
        if(role!=Role::MeshJob)readers+=delta;
        if(role!=Role::IconRender)borrowers+=delta;
    }
    void release(Role role){std::lock_guard lock(mutex);adjust(role,-1);advance();publish();}
    void advance() {
        if(cancel && drained() && (phase==WorldPackReloadPhase::Pausing || phase==WorldPackReloadPhase::Prepared))phase=WorldPackReloadPhase::Cancelled;
        if(phase==WorldPackReloadPhase::Reacquiring && retired[0] && retired[1] && ready[0] && ready[1] && drained() && gpuDone()) {
            phase=WorldPackReloadPhase::Ready;retained=false;error=Error::None;
        }
    }
    void invalidateLocked(Error why){
        if(!modulePin)modulePin=module.lock();
        phase=WorldPackReloadPhase::Invalidated;closed=true;retained=true;pendingOwner={};error=why;
    }
    void publish() noexcept {
        try {
        auto value=std::make_shared<Snapshot>();value->serial=serial;value->oldEpoch=request.oldEpoch;value->nextEpoch=request.nextEpoch;
        value->ready=ready;value->retired=retired;value->domainGpu=gpu;value->enabled=enabled;value->closed=closed;
        auto& r=value->reply;r.token=request.token;r.world=request.world;r.client=request.client;r.renderer=request.renderer;r.epoch=epoch;
        r.phase=enabled?phase:WorldPackReloadPhase::Unknown;
        r.ownedGpu=gpuDone()?(gpu[0]==OwnedGpuFence::NativeManaged?OwnedGpuFence::NativeManaged
            :(gpu[0]==OwnedGpuFence::NoOwnedSubmission && gpu[1]==OwnedGpuFence::NoOwnedSubmission?OwnedGpuFence::NoOwnedSubmission:OwnedGpuFence::Complete))
            :(gpu[0]==OwnedGpuFence::Unknown || gpu[1]==OwnedGpuFence::Unknown
                || (gpu[0]==OwnedGpuFence::NativeManaged && (!nativeManaged || nativeOwner!=owner))?OwnedGpuFence::Unknown:OwnedGpuFence::Pending);
        r.cpuReaders=readers;r.asyncJobs=jobs;r.nativeBorrowers=borrowers;r.admissionClosed=closed;r.oldOwnersRetained=retained;
        r.error=static_cast<std::uint32_t>(error);r.rejectedOldPublications=rejected;
        published.store(std::move(value),std::memory_order_release);
        } catch(...) {
            invalidateLocked(Error::Busy);published.store(emergency,std::memory_order_release);
        }
    }
    std::mutex mutex;
    std::atomic<std::shared_ptr<Snapshot const>> published;
    std::shared_ptr<Snapshot> emergency;
    WorldPackReloadRequest request;
    Owner owner,attemptOwner,nativeOwner,pendingOwner;
    WorldPackReloadPhase phase{WorldPackReloadPhase::Idle};
    WorldPackReloadCommand lastCompletion{WorldPackReloadCommand::Query};
    Error error{};
    std::uint64_t epoch{1},serial{},rejected{};
    std::uint32_t readers{},jobs{},borrowers{};
    bool enabled{},closed{},retained{},cancel{},nativeManaged{};
    std::array<OwnedGpuFence,2> gpu{OwnedGpuFence::NoOwnedSubmission,OwnedGpuFence::NoOwnedSubmission};
    std::array<bool,2> ready{},retired{};
    std::weak_ptr<void> module;
    std::shared_ptr<void> modulePin;
    std::set<std::uint64_t> tokens;
};
inline State& state(){static State value;return value;}
}
