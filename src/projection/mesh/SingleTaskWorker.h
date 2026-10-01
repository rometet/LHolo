#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace lholo::projection::detail {

// One accepted task at a time, with explicit admission and no allocating queue.
// The owner stops/joins before releasing any native world referenced by a task.
// stop() belongs to the owner thread; tasks must not destroy/stop their executor.
class SingleTaskWorker {
public:
    SingleTaskWorker() : mThread([this] { run(); }) {}
    ~SingleTaskWorker() { stop(); }
    SingleTaskWorker(SingleTaskWorker const&) = delete;
    SingleTaskWorker& operator=(SingleTaskWorker const&) = delete;

    template <class Task>
    bool submit(Task&& task) {
        static_assert(std::is_nothrow_invocable_v<Task>);
        // Allocation, if needed, happens before admission. A throwing conversion
        // leaves the worker unchanged and is handled by the submission owner.
        std::function<void()> accepted(std::forward<Task>(task));
        std::lock_guard lock(mMutex);
        if (mStopping || mBusy) return false;
        mTask = std::move(accepted); // std::function move assignment is noexcept.
        mBusy = true;
        mReady.notify_one();
        return true;
    }

    void stop() {
        std::lock_guard joinLock(mJoinMutex);
        {
            std::lock_guard lock(mMutex);
            mStopping = true;
        }
        mReady.notify_one();
        if (mThread.joinable()) mThread.join();
    }

private:
    void run() noexcept {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mMutex);
                mReady.wait(lock, [&] { return mStopping || static_cast<bool>(mTask); });
                if (!mTask) return;
                task = std::move(mTask);
            }
            task(); // Admission requires a noexcept callable (WorkerTaskBoundary).
            // Destroy captured native references before releasing the slot.
            task = {};
            {
                std::lock_guard lock(mMutex);
                mBusy = false;
                if (mStopping) return;
            }
        }
    }

    std::mutex mMutex;
    std::mutex mJoinMutex;
    std::condition_variable mReady;
    std::function<void()> mTask;
    bool mStopping{};
    bool mBusy{};
    std::thread mThread;
};

} // namespace lholo::projection::detail
