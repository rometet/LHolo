#include <Windows.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <dxgi1_4.h>

#include "overlay/D3D12QueueBinding.h"
#include "CursorThreadChecks.h"
#include "GameMouseHandoffChecks.h"
#include "HookRetryChecks.h"
#include "HookChainChecks.h"
#include "DeferredHookChecks.h"
#include "HookRetirementChecks.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using lholo::overlay::detail::D3D12QueueBinding;
using lholo::overlay::detail::queueMatchesDevice;

namespace {
unsigned checks{}, failures{}, crossDeviceChecks{};
unsigned removalChecks{};

bool check(bool result, char const* message) {
    ++checks;
    if (!result) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return result;
}

bool checkHr(HRESULT result, char const* message) {
    if (FAILED(result)) std::fprintf(stderr, "HRESULT=0x%08lX ", static_cast<unsigned long>(result));
    return check(SUCCEEDED(result), message);
}

ComPtr<ID3D12CommandQueue> makeQueue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type) {
    D3D12_COMMAND_QUEUE_DESC description{};
    description.Type = type;
    ComPtr<ID3D12CommandQueue> queue;
    checkHr(device.CreateCommandQueue(&description, IID_PPV_ARGS(&queue)), "create real command queue");
    return queue;
}

bool drawAndRead(ID3D12Device& device, ID3D12CommandQueue& queue) {
    // Do not exercise an invalid API pairing even in the before-fix run.
    if (!check(queueMatchesDevice(queue, device), "interop queue belongs to target device")) return false;
    IUnknown* queues[]{&queue};
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context;
    if (!checkHr(D3D11On12CreateDevice(&device, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
        queues, 1, 0, &device11, &context, nullptr), "create D3D11On12 backend")) return false;
    ComPtr<ID3D11On12Device> on12;
    if (!checkHr(device11.As(&on12), "query On12 interface")) return false;

    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = 8;
    texture.Height = 8;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = texture.Format;
    clear.Color[0] = clear.Color[2] = clear.Color[3] = 1.0f;
    ComPtr<ID3D12Resource> resource;
    if (!checkHr(device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&resource)), "create native render target")) return false;
    D3D11_RESOURCE_FLAGS flags{};
    flags.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Resource> wrapped;
    if (!checkHr(on12->CreateWrappedResource(resource.Get(), &flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COMMON, IID_PPV_ARGS(&wrapped)), "wrap native render target")) return false;
    ComPtr<ID3D11RenderTargetView> target;
    if (!checkHr(device11->CreateRenderTargetView(wrapped.Get(), nullptr, &target), "create wrapped render view")) return false;

    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width = stagingDesc.Height = 8;
    stagingDesc.MipLevels = stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (!checkHr(device11->CreateTexture2D(&stagingDesc, nullptr, &staging), "create CPU readback texture")) return false;

    ID3D11Resource* resources[]{wrapped.Get()};
    on12->AcquireWrappedResources(resources, 1);
    context->ClearRenderTargetView(target.Get(), clear.Color);
    context->CopyResource(staging.Get(), wrapped.Get());
    on12->ReleaseWrappedResources(resources, 1);
    context->Flush();

    ComPtr<ID3D12Fence> fence;
    if (!checkHr(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "create completion fence")) return false;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!check(event != nullptr, "create completion event")) return false;
    auto const signaled = checkHr(queue.Signal(fence.Get(), 1), "signal interop queue")
        && checkHr(fence->SetEventOnCompletion(1, event), "register queue completion");
    auto const completed = signaled && check(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "interop queue completes");
    CloseHandle(event);
    if (!completed) return false;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!checkHr(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "map actual GPU readback")) return false;
    std::array<unsigned char, 4> const expected{255, 0, 255, 255};
    for (unsigned y = 0; y < 8; ++y) {
        auto const* row = static_cast<unsigned char const*>(mapped.pData) + y * mapped.RowPitch;
        for (unsigned x = 0; x < 8; ++x) {
            for (unsigned channel = 0; channel < expected.size(); ++channel) {
                check(row[x * 4 + channel] == expected[channel], "native clear/readback pixel preserved");
            }
        }
    }
    context->Unmap(staging.Get(), 0);
    checkHr(device.GetDeviceRemovedReason(), "interop target device remains usable");
    return true;
}
} // namespace

int main(int argc, char** argv) {
    bool const warpOnly = argc == 2 && std::strcmp(argv[1], "--warp-only") == 0;
    if (argc > 2 || (argc == 2 && !warpOnly)) {
        std::fprintf(stderr, "Usage: LHoloGraphicsTests [--warp-only]\n");
        return 2;
    }
    lholo::tests::cursor::runWindowCursorChecks(check);
    lholo::tests::mouse_handoff::runGameMouseHandoffChecks(check);
    lholo::tests::hook_retry::runHookRetryChecks(check);
    if (!lholo::tests::hook_chain::runHookChainChecks(check)) return 1;
    if (!lholo::tests::deferred_hook::runDeferredHookChecks(check)) return 1;
    if (!lholo::tests::hook_retirement::runHookRetirementChecks(check)) return 1;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    if (!checkHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "create DXGI factory")
        || !checkHr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "find WARP adapter")
        || !checkHr(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "create WARP device")) return 1;
    auto direct = makeQueue(*device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto copy = makeQueue(*device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);
    auto compute = makeQueue(*device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
    if (!direct || !copy || !compute) return 1;
    D3D12QueueBinding binding;
    check(!binding.captured() && !binding.get(), "empty binding has no captured queue");
    check(!binding.capture(*copy.Get()), "copy queue cannot claim overlay");
    check(!binding.capture(*compute.Get()), "compute queue cannot claim overlay");
    check(!binding.bindDevice(*device.Get()), "target waits until an eligible queue executes");
    check(binding.capture(*direct.Get()), "matching direct queue captured");
    check(binding.captured() && binding.get() == direct.Get(), "captured queue published");
    check(binding.bindDevice(*device.Get()), "same target keeps established queue");
    check(!binding.capture(*direct.Get()), "steady-state capture is idempotent");
    if (binding.get()) drawAndRead(*device.Get(), *binding.get());
    binding.reset();
    check(!binding.captured() && !binding.get(), "reset releases captured queue");

    ComPtr<ID3D12Device> foreignDevice;
    auto const hasHardware = !warpOnly
        && SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&foreignDevice)));
    auto foreignQueue = hasHardware ? makeQueue(*foreignDevice.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT) : ComPtr<ID3D12CommandQueue>{};
    if (foreignQueue && !queueMatchesDevice(*foreignQueue.Get(), *device.Get())) {
        auto crossCheck = [](bool result, char const* message) { ++crossDeviceChecks; return check(result, message); };
        crossCheck(binding.capture(*foreignQueue.Get()), "unbound capture initially accepts first direct queue");
        crossCheck(!binding.bindDevice(*device.Get()), "first foreign queue retired when target becomes known");
        crossCheck(!binding.captured() && !binding.get(), "foreign queue cannot remain published");
        crossCheck(!binding.capture(*foreignQueue.Get()), "pending target rejects repeated foreign execution");
        crossCheck(binding.capture(*direct.Get()), "correct target queue recaptured after foreign queue");
        bool const matched = binding.get() && queueMatchesDevice(*binding.get(), *device.Get());
        crossCheck(matched, "recovered queue belongs to actual swap-chain device");
        if (matched) drawAndRead(*device.Get(), *binding.get());
        crossCheck(!binding.bindDevice(*foreignDevice.Get()), "device replacement retires previous target queue");
        crossCheck(!binding.capture(*direct.Get()), "old target cannot poison replacement capture");
        crossCheck(binding.capture(*foreignQueue.Get()), "replacement target queue captured");
        bool const replacementMatched = binding.get() && queueMatchesDevice(*binding.get(), *foreignDevice.Get());
        crossCheck(replacementMatched, "replacement queue belongs to new device");
        if (replacementMatched) drawAndRead(*foreignDevice.Get(), *binding.get());
    } else {
        std::puts(warpOnly ? "WARP-only requested; cross-device checks excluded."
            : "Cross-device checks unavailable: no distinct hardware device; WARP checks still run.");
    }
    binding.reset();
    for (unsigned i = 0; i < 100; ++i) {
        check(!binding.bindDevice(*device.Get()), "empty lifecycle starts waiting for target");
        check(binding.capture(*direct.Get()) && binding.bindDevice(*device.Get()), "repeated matching lifecycle");
        binding.reset();
        check(!binding.captured() && !binding.get(), "repeated lifecycle returns to empty state");
    }
    // Remove only this console's WARP device. A retained On12 backend or queue
    // pins the removed per-adapter singleton, preventing native recreation.
    ComPtr<ID3D12Device5> removable;
    if (SUCCEEDED(device.As(&removable))) {
        auto removalCheck = [](bool result, char const* message) { ++removalChecks; return check(result, message); };
        if (!check(binding.capture(*direct.Get()) && binding.bindDevice(*device.Get()),
            "native removal starts from an established queue binding")) return 1;
        ComPtr<ID3D11Device> device11;
        ComPtr<ID3D11DeviceContext> context;
        IUnknown* queues[]{binding.get()};
        if (!checkHr(D3D11On12CreateDevice(device.Get(), D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
            queues, 1, 0, &device11, &context, nullptr), "create backend for native device removal")) return 1;
        bool cleanupCalled{};
        removalCheck(!binding.retireAfterDeviceLoss(S_OK, [&] { cleanupCalled = true; }), "successful presentation keeps backend");
        removalCheck(!binding.retireAfterDeviceLoss(DXGI_ERROR_WAS_STILL_DRAWING, [&] { cleanupCalled = true; }), "temporary presentation result keeps backend");
        removalCheck(!cleanupCalled && binding.captured(), "healthy results do not invoke retirement");
        removable->RemoveDevice();
        auto const reason = device->GetDeviceRemovedReason();
        removalCheck(reason == DXGI_ERROR_DEVICE_REMOVED, "real WARP device reports removal");
        D3D12QueueBinding removedBinding;
        removalCheck(!removedBinding.capture(*direct.Get()), "removed queue cannot be captured again during recovery");
        removalCheck(!removedBinding.bindDevice(*device.Get()) && !removedBinding.get(), "removed target cannot retain pending device ownership");
        removable.Reset();
        direct.Reset();
        copy.Reset();
        compute.Reset();
        foreignQueue.Reset();
        foreignDevice.Reset();
        device.Reset();
        ComPtr<ID3D12Device> recreated;
        removalCheck(FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&recreated))),
            "retained overlay resources prevent removed device recreation");
        bool cleanupFailureObserved{};
        try {
            binding.retireAfterDeviceLoss(reason, [] { throw std::runtime_error("backend retirement failed"); });
        } catch (std::runtime_error const&) {
            cleanupFailureObserved = true;
        }
        removalCheck(cleanupFailureObserved && binding.captured() && binding.get(), "failed backend retirement preserves queue and retry state");
        removalCheck(binding.retireAfterDeviceLoss(reason, [&] {
            removalCheck(binding.captured() && binding.get(), "final backend Flush keeps detour fast path active");
            context->ClearState();
            context->Flush();
            context.Reset();
            device11.Reset();
            cleanupCalled = true;
        }), "removed backend and queue retired together");
        removalCheck(cleanupCalled && !binding.captured() && !binding.get(), "retired overlay releases old device ownership");
        bool const created = checkHr(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&recreated)),
            "WARP device recreated after overlay retirement");
        if (created) {
            auto replacementQueue = makeQueue(*recreated.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
            if (replacementQueue) {
                removalCheck(!binding.bindDevice(*recreated.Get()) && binding.capture(*replacementQueue.Get()), "fresh device accepts fresh queue");
                if (binding.get() && queueMatchesDevice(*binding.get(), *recreated.Get())) {
                    drawAndRead(*recreated.Get(), *binding.get());
                }
            }
        }
        binding.reset();
    } else {
        std::puts("Native device removal checks unavailable: ID3D12Device5 is unsupported.");
    }
    std::printf("GraphicsInteropTests: %u checks, %u failures, %u cross-device checks, %u removal checks\n",
        checks, failures, crossDeviceChecks, removalChecks);
    return failures ? 1 : 0;
}
