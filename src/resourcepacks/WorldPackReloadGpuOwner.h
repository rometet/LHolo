#pragma once
#include "resourcepacks/WorldPackReloadState.h"
#include "overlay/BlockIconGpu.h"
#include "ui/BlockIconRuntime.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace lholo::resourcepacks {
// Called only by the existing Present owner with the overlay resource/context
// locks held. This event covers LHolo's D3D11 icon resources. It is deliberately
// NOT a receipt for native projection meshes submitted through FrameBuilder.
class GpuOwner {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Query> query;
    std::uint64_t serial{};
    std::uint64_t rebuildRevision{};
    bool oldComplete{},retired{},viewsBuilt{},newComplete{};
    std::vector<ui::icons::Key> keys;
    bool poll(State& owner,std::uint64_t ticket) {
        BOOL done=FALSE;
        auto const result=context->GetData(query.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(FAILED(result)){owner.invalidate();return false;}
        if(result!=S_OK || !done)return false;
        owner.gpuReceipt(Domain::Icons,ticket,OwnedGpuFence::Complete);
        query.Reset();return true;
    }
    bool issue(State& owner,std::uint64_t ticket) {
        D3D11_QUERY_DESC description{D3D11_QUERY_EVENT,0};
        if(FAILED(device->CreateQuery(&description,&query))){owner.invalidate();return false;}
        context->End(query.Get());context->Flush();
        owner.gpuReceipt(Domain::Icons,ticket,OwnedGpuFence::Pending);return true;
    }
public:
    void pump(State& owner,overlay::BlockIconGpu& icons,ID3D11Device* current,ID3D11DeviceContext* immediate) {
        auto view=owner.snapshot();
        if(!view->closed) {
            // Before-native cancellation preserves the same resource owners.
            // D3D retains submitted work; only our event/context pins retire.
            query.Reset();context.Reset();device.Reset();serial=0;keys.clear();return;
        }
        if(view->reply.phase==WorldPackReloadPhase::Invalidated)return;
        if(!current || !immediate || immediate->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){owner.invalidate();return;}
        Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
        immediate->GetDevice(&contextDevice);
        if(contextDevice.Get()!=current || (device && device.Get()!=current)) {owner.invalidate();return;}
        if(view->serial!=serial) {
            // A new attempt may only begin after settlement or cancellation.
            // A pending upload from Reacquiring remains pending in State and
            // is fenced again on the same actual D3D11 immediate context.
            query.Reset();device=current;context=immediate;serial=view->serial;
            oldComplete=false;retired=false;viewsBuilt=false;newComplete=false;rebuildRevision=0;
        }
        auto const phase=view->reply.phase;
        if(retired && (phase==WorldPackReloadPhase::Reacquiring || phase==WorldPackReloadPhase::Ready)
            && ui::icons::store().revision()!=rebuildRevision) {owner.invalidate();return;}
        if(phase==WorldPackReloadPhase::Pausing) {
            if(view->reply.cpuReaders || view->reply.asyncJobs || view->reply.nativeBorrowers)return;
            if(view->domainGpu[1]==OwnedGpuFence::NoOwnedSubmission) {oldComplete=true;return;}
            if(oldComplete)return;
            if(!query && !issue(owner,serial))return;
            if(query && poll(owner,serial))oldComplete=true;
            return;
        }
        if(phase!=WorldPackReloadPhase::Reacquiring || view->ready[1])return;
        auto lease=owner.enter(Role::IconRender,true);
        if(!lease || lease.serial!=serial || !owner.mayPublish(lease))return;
        if(!retired) {
            // Prepare already required both old owned domains' GPU receipts.
            auto currentKeys=icons.retireForReload();
            keys.insert(keys.end(),currentKeys.begin(),currentKeys.end());
            std::sort(keys.begin(),keys.end());keys.erase(std::unique(keys.begin(),keys.end()),keys.end());
            owner.retiredOwner(Domain::Icons,serial);
            owner.gpuReceipt(Domain::Icons,serial,OwnedGpuFence::NoOwnedSubmission);
            ui::icons::startResourceReload(view->reply.epoch,serial,keys);
            rebuildRevision=ui::icons::store().revision();retired=true;
        }
        if(!ui::icons::resourceReloadReady(serial))return;
        if(!viewsBuilt) {
            if(!icons.rebuildViews(current,keys))return;
            viewsBuilt=true;
            if(icons.cached()==0) {
                newComplete=true;owner.gpuReceipt(Domain::Icons,serial,OwnedGpuFence::NoOwnedSubmission);
            } else if(!issue(owner,serial))return;
        }
        if(query && poll(owner,serial))newComplete=true;
        if(newComplete)owner.rebuilt(Domain::Icons,serial,true);
    }
};
inline GpuOwner& gpuOwner(){static GpuOwner value;return value;}
}
