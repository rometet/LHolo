#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <atomic>
#include <utility>

namespace lholo::overlay::detail {

inline bool queueMatchesDevice(ID3D12CommandQueue& queue, ID3D12Device& device) noexcept {
    Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
    Microsoft::WRL::ComPtr<IUnknown> queueIdentity, deviceIdentity;
    return SUCCEEDED(queue.GetDevice(IID_PPV_ARGS(&queueDevice)))
        && SUCCEEDED(queueDevice.As(&queueIdentity))
        && SUCCEEDED(device.QueryInterface(IID_PPV_ARGS(&deviceIdentity)))
        && queueIdentity.Get() == deviceIdentity.Get();
}

// The resource mutex owns all COM state. The atomic flag is only the detour's
// fast path: an established D3D11On12 backend can synchronously execute this
// queue during Flush while the resource mutex is held.
class D3D12QueueBinding {
public:
    bool captured() const noexcept { return mCaptured.load(std::memory_order_acquire); }
    ID3D12CommandQueue* get() const noexcept { return mQueue.Get(); }

    bool capture(ID3D12CommandQueue& queue) noexcept {
        if (mQueue || queue.GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return false;
        Microsoft::WRL::ComPtr<ID3D12Device> candidateDevice;
        if (FAILED(queue.GetDevice(IID_PPV_ARGS(&candidateDevice)))
            || FAILED(candidateDevice->GetDeviceRemovedReason())) return false;
        if (mPendingDevice && !queueMatchesDevice(queue, *mPendingDevice.Get())) return false;
        mQueue = &queue;
        mPendingDevice.Reset();
        mCaptured.store(true, std::memory_order_release);
        return true;
    }

    // Call only after releasing/flushing the old D3D11 backend. Clearing the
    // fast flag earlier would deadlock its synchronous ExecuteCommandLists.
    bool bindDevice(ID3D12Device& device) noexcept {
        if (FAILED(device.GetDeviceRemovedReason())) {
            reset();
            return false;
        }
        if (mQueue && queueMatchesDevice(*mQueue.Get(), device)) {
            mPendingDevice.Reset();
            return true;
        }
        mPendingDevice = &device;
        mQueue.Reset();
        mCaptured.store(false, std::memory_order_release);
        return false;
    }

    void reset() noexcept {
        mQueue.Reset();
        mPendingDevice.Reset();
        mCaptured.store(false, std::memory_order_release);
    }

    template <class ReleaseBackend>
    bool retireAfterDeviceLoss(HRESULT result, ReleaseBackend&& releaseBackend) {
        if (result != DXGI_ERROR_DEVICE_REMOVED && result != DXGI_ERROR_DEVICE_RESET) return false;
        // Keep the capture flag and queue alive through the final On12 Flush.
        // If backend retirement throws, preserve tracking so it can be retried.
        std::forward<ReleaseBackend>(releaseBackend)();
        reset();
        return true;
    }

private:
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mQueue;
    Microsoft::WRL::ComPtr<ID3D12Device> mPendingDevice;
    std::atomic_bool mCaptured{};
};

} // namespace lholo::overlay::detail
