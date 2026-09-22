// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/mesh/ProjectionMeshWorker.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <mutex>
#include <utility>

#include "ll/api/thread/ThreadPoolExecutor.h"

namespace lholo::projection::detail {
namespace {

std::mutex                                      gMeshWorkerMutex;
std::mutex                                      gMeshWorkerLifecycleMutex;
std::deque<AsyncSectionBuildResult>             gCompletedSectionBuilds;
std::unique_ptr<ll::thread::ThreadPoolExecutor> gMeshWorkerExecutor;
std::atomic_bool                                gMeshWorkerBusy{};
std::atomic_uint64_t                            gMeshWorkerGeneration{1};
std::atomic_bool                                gMeshWorkerDisabledForSession{};

} // namespace

std::uint64_t startMeshWorker() {
    std::lock_guard lifecycleLock(gMeshWorkerLifecycleMutex);
    if (!gMeshWorkerExecutor) {
        gMeshWorkerExecutor = std::make_unique<ll::thread::ThreadPoolExecutor>(
            "LHoloProjectionMesh", 1
        );
    }
    gMeshWorkerBusy.store(false, std::memory_order_release);
    return gMeshWorkerGeneration.load(std::memory_order_acquire);
}

void stopMeshWorker() {
    std::lock_guard lifecycleLock(gMeshWorkerLifecycleMutex);
    auto const nextGeneration = gMeshWorkerGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    (void)nextGeneration;
    if (gMeshWorkerExecutor) {
        // destroy() drains the executor and joins its sole worker. Tasks never
        // acquire the active projection mutex, so this is safe while state is
        // detached from the active world.
        gMeshWorkerExecutor->destroy();
        gMeshWorkerExecutor.reset();
    }
    gMeshWorkerBusy.store(false, std::memory_order_release);
    std::lock_guard lock(gMeshWorkerMutex);
    gCompletedSectionBuilds.clear();
}

bool submitMeshWorkerTask(
    std::uint64_t workerGeneration,
    std::function<AsyncSectionBuildResult()> task
) {
    // Keep the executor alive through the execute() call. The world listener
    // may stop the worker from another engine thread while the render thread
    // is submitting its next section.
    std::lock_guard lifecycleLock(gMeshWorkerLifecycleMutex);
    if (!gMeshWorkerExecutor
        || workerGeneration != gMeshWorkerGeneration.load(std::memory_order_acquire)) {
        return false;
    }
    bool expected = false;
    if (!gMeshWorkerBusy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    try {
        gMeshWorkerExecutor->execute([
            workerGeneration,
            task = std::move(task)
        ]() mutable {
            AsyncSectionBuildResult result;
            try {
                result = task();
            } catch (std::exception const& exception) {
                result.workerGeneration = workerGeneration;
                result.success = false;
                result.failureReason = std::string{"uncaught task exception: "} + exception.what();
            } catch (...) {
                result.workerGeneration = workerGeneration;
                result.success = false;
                result.failureReason = "uncaught non-standard task exception";
            }
            if (workerGeneration == gMeshWorkerGeneration.load(std::memory_order_acquire)) {
                std::lock_guard lock(gMeshWorkerMutex);
                gCompletedSectionBuilds.emplace_back(std::move(result));
            }
            gMeshWorkerBusy.store(false, std::memory_order_release);
        });
    } catch (...) {
        gMeshWorkerBusy.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

std::vector<AsyncSectionBuildResult> takeCompletedSectionBuilds(std::size_t limit) {
    std::vector<AsyncSectionBuildResult> results;
    std::lock_guard lock(gMeshWorkerMutex);
    auto const count = std::min(limit, gCompletedSectionBuilds.size());
    results.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        results.emplace_back(std::move(gCompletedSectionBuilds.front()));
        gCompletedSectionBuilds.pop_front();
    }
    return results;
}

bool meshWorkerIsBusy() {
    return gMeshWorkerBusy.load(std::memory_order_acquire);
}

bool meshWorkerIsDisabledForSession() {
    return gMeshWorkerDisabledForSession.load(std::memory_order_acquire);
}

void disableMeshWorkerForSession() {
    gMeshWorkerDisabledForSession.store(true, std::memory_order_release);
}

void resetMeshWorkerForSession() {
    gMeshWorkerDisabledForSession.store(false, std::memory_order_release);
}

} // namespace lholo::projection::detail
