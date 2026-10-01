#pragma once

#include "app/HookLifecycle.h"
#include "app/ScopeExit.h"
#include "overlay/NativeHookBinding.h"
#include "projection/mesh/SingleTaskWorker.h"
#include "projection/mesh/WorkerTaskBoundary.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <thread>

namespace lholo::tests::hook_retirement {
using Function = int (*)(int);
inline Function original{};
inline std::atomic_uint nativeWrites{};
inline thread_local bool suppressWrites{};

struct CapturedOwner {
    std::promise<bool>& released;
    explicit CapturedOwner(std::promise<bool>& result) : released(result) {}
    ~CapturedOwner() {
        app::hook_lifecycle::DetourGuard nested;
        released.set_value(static_cast<bool>(nested));
    }
};

__declspec(noinline) inline int nativeWrite(int value) {
    nativeWrites.fetch_add(1, std::memory_order_relaxed);
    volatile int retained = value;
    return retained;
}
__declspec(noinline) inline int writeHook(int value) {
    app::hook_lifecycle::DetourGuard guard;
    if (guard && suppressWrites) return 0;
    return original(value);
}

template <class Check>
bool runHookRetirementChecks(Check&& check) {
    using namespace app::hook_lifecycle;
    using namespace std::chrono_literals;
    if (!check(state() == State::Disabled && MH_Initialize() == MH_OK, "start isolated native retirement registry")) return false;
    overlay::detail::NativeHookBinding binding;
    void* trampoline{};
    if (!check(binding.create(reinterpret_cast<void*>(&nativeWrite), reinterpret_cast<void*>(&writeHook), &trampoline) == MH_OK,
        "create real suppression-dependent native hook")) return false;
    original = reinterpret_cast<Function>(trampoline);
    if (!check(binding.enable() == MH_OK && beginEnable(), "admit native worker while dependency hook is installed")) return false;
    nativeWrites.store(0, std::memory_order_release);
    std::promise<bool> started;
    std::promise<void> resumeTask;
    std::promise<int> taskResult;
    std::promise<bool> workerStarted;
    std::promise<void> resumeWorker;
    std::promise<bool> workerResult;
    std::promise<bool> capturedOwnerReleased;
    std::promise<void> quiesced;
    std::promise<bool> originStarted;
    std::promise<void> resumeOrigin;
    std::promise<bool> physicallyDisabled;
    std::promise<void> runningDrained;
    std::promise<bool> finished;
    auto taskStarted = started.get_future();
    auto taskResume = resumeTask.get_future();
    auto result = taskResult.get_future();
    auto workerEntered = workerStarted.get_future();
    auto workerResume = resumeWorker.get_future();
    auto workerNested = workerResult.get_future();
    auto ownerReleased = capturedOwnerReleased.get_future();
    auto closedAdmission = quiesced.get_future();
    auto originEntered = originStarted.get_future();
    auto originResume = resumeOrigin.get_future();
    auto removedDependency = physicallyDisabled.get_future();
    auto completedRunningDrain = runningDrained.get_future();
    auto retired = finished.get_future();
    std::atomic_bool beginPhysicalRetirement{};
    bool taskReleased{}, originReleased{}, workerReleased{};
    projection::detail::SingleTaskWorker worker;
    std::atomic_bool busy{true};
    std::thread stopper, originThread, renderThread;
    app::ScopeExit cleanup([&]() noexcept {
        if (!taskReleased) resumeTask.set_value();
        if (!originReleased) resumeOrigin.set_value();
        if (!workerReleased) resumeWorker.set_value();
        beginPhysicalRetirement.store(true, std::memory_order_release);
        beginPhysicalRetirement.notify_all();
        if (stopper.joinable()) stopper.join();
        if (originThread.joinable()) originThread.join();
        if (renderThread.joinable()) renderThread.join();
        worker.stop();
        binding.disable();
        binding.remove();
        beginQuiesce();
        waitForQuiescence();
        markDisabled();
        MH_Uninitialize();
        original = nullptr;
    });
    std::function<bool()> nativeTask = [&, owner = std::make_shared<CapturedOwner>(capturedOwnerReleased)]() {
        (void)owner;
        workerStarted.set_value(insideDetour());
        workerResume.wait();
        DetourGuard nestedLease;
        return static_cast<bool>(nestedLease);
    };
    if (!check(worker.submit([&, task = std::move(nativeTask)]() mutable noexcept {
        projection::detail::runLeasedWorkerTaskBoundary(busy, task,
            [&](bool nested) { workerResult.set_value(nested); },
            [&]() noexcept { workerResult.set_value(false); });
    }), "submit real worker with a full-task lifecycle lease")) return false;
    if (!check(workerEntered.wait_for(5s) == std::future_status::ready && workerEntered.get(), "worker body holds an admitted Running lease")) return false;
    renderThread = std::thread([&] {
        // An admitted render callback is independent of the worker: joining
        // the worker alone must not remove this callback's nested dependency.
        DetourGuard renderLease;
        suppressWrites = true;
        started.set_value(static_cast<bool>(renderLease));
        taskResume.wait();
        taskResult.set_value(nativeWrite(42));
        suppressWrites = false;
    });
    if (!check(taskStarted.wait_for(5s) == std::future_status::ready && taskStarted.get(), "native task holds an admitted Running lease")) return false;
    stopper = std::thread([&] {
        beginQuiesce();
        quiesced.set_value();
        beginPhysicalRetirement.wait(false, std::memory_order_acquire);
        waitForRunningCallbacks();
        runningDrained.set_value();
        worker.stop();
        bool const disabled = binding.disable();
        physicallyDisabled.set_value(disabled);
        // Physical disable prevents new native hook entries. Existing
        // origin-only callbacks still borrow this DLL/trampoline until drained.
        waitForQuiescence();
        bool const removed = disabled && binding.remove();
        markDisabled();
        finished.set_value(removed);
    });
    if (!check(closedAdmission.wait_for(5s) == std::future_status::ready, "retirement closes Running admission")) return false;
    originThread = std::thread([&] {
        DetourGuard passThrough;
        originStarted.set_value(!static_cast<bool>(passThrough));
        originResume.wait();
    });
    if (!check(originEntered.wait_for(5s) == std::future_status::ready && originEntered.get(), "new origin-only callback holds a separate lifetime lease")) return false;
    beginPhysicalRetirement.store(true, std::memory_order_release);
    beginPhysicalRetirement.notify_all();
    resumeWorker.set_value();
    workerReleased = true;
    if (!check(workerNested.wait_for(5s) == std::future_status::ready && workerNested.get(),
        "admitted worker's nested queries inherit Running during quiescence")) return false;
    if (!check(ownerReleased.wait_for(5s) == std::future_status::ready && ownerReleased.get(),
        "captured native owner is destroyed inside the worker's Running lease")) return false;
    auto const prematureDrain = completedRunningDrain.wait_for(30ms);
    check(prematureDrain == std::future_status::timeout, "Running body keeps its physical native dependency installed");
    if (prematureDrain == std::future_status::ready) {
        // Exercise the unsafe ordering only in the before-policy run. Waiting
        // for actual disable makes the native-write failure observable rather
        // than depending on worker-exit/MinHook freeze timing.
        if (!check(removedDependency.wait_for(5s) == std::future_status::ready,
            "before-policy native disable completes while body remains live")) return false;
    }
    resumeTask.set_value();
    taskReleased = true;
    if (!check(result.wait_for(5s) == std::future_status::ready, "admitted native task finishes during quiescence")) return false;
    check(result.get() == 0 && nativeWrites.load(std::memory_order_acquire) == 0,
        "nested native write remains suppressed until admitted task finishes");
    if (!check(removedDependency.wait_for(5s) == std::future_status::ready, "Running drain ignores separately held origin-only callback")) return false;
    check(removedDependency.get(), "physical native hook disables after Running drain and worker join");
    check(retired.wait_for(30ms) == std::future_status::timeout, "final DLL/trampoline drain still waits for origin-only callback");
    resumeOrigin.set_value();
    originReleased = true;
    if (!check(retired.wait_for(5s) == std::future_status::ready, "complete retirement finishes after origin-only release")) std::_Exit(1);
    check(retired.get(), "trampoline removed only after both retirement phases");
    stopper.join();
    originThread.join();
    renderThread.join();
    check(state() == State::Disabled, "native retirement returns lifecycle to Disabled");
    std::atomic_bool canceledBusy{true};
    unsigned canceledInvocations{}, canceledPublications{}, canceledFailures{};
    std::function<int()> canceledTask = [&] { ++canceledInvocations; return 1; };
    projection::detail::runLeasedWorkerTaskBoundary(canceledBusy, canceledTask,
        [&](int) { ++canceledPublications; }, [&]() noexcept { ++canceledFailures; });
    check(!canceledTask && !canceledBusy.load(std::memory_order_acquire)
        && canceledInvocations == 0 && canceledPublications == 0 && canceledFailures == 0,
        "task starting after shutdown releases its callable without native work or publication");
    auto const priorWrites = nativeWrites.load(std::memory_order_acquire);
    check(nativeWrite(7) == 7 && nativeWrites.load(std::memory_order_acquire) == priorWrites + 1,
        "retired hook restores native callback behavior");
    return true;
}
} // namespace lholo::tests::hook_retirement
