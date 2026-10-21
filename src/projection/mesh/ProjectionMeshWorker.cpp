// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/mesh/ProjectionMeshWorker.h"
#include "resourcepacks/WorldPackReloadState.h"
#include "projection/mesh/WorkerTaskBoundary.h"
#include "projection/mesh/SingleTaskWorker.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <mutex>
#include <utility>

namespace lholo::projection::detail {
namespace {

std::mutex                                      gMeshWorkerMutex;
std::mutex                                      gMeshWorkerLifecycleMutex;
std::deque<AsyncSectionBuildResult>             gCompletedSectionBuilds;
std::vector<CancelledSectionBuild>              gCancelledSectionBuilds;
std::unique_ptr<SingleTaskWorker>               gMeshWorkerExecutor;
std::atomic_bool                                gMeshWorkerBusy{};
std::atomic_uint64_t                            gMeshWorkerGeneration{1};
std::atomic_bool                                gMeshWorkerDisabledForSession{};
std::atomic_bool                                gMeshWorkerFatalFailure{};

} // namespace

std::uint64_t startMeshWorker() {
    std::lock_guard lifecycleLock(gMeshWorkerLifecycleMutex);
    if (!gMeshWorkerExecutor) {
        gMeshWorkerExecutor = std::make_unique<SingleTaskWorker>();
        gMeshWorkerBusy.store(false, std::memory_order_release);
        gMeshWorkerFatalFailure.store(false, std::memory_order_release);
    }
    return gMeshWorkerGeneration.load(std::memory_order_acquire);
}

void stopMeshWorker() {
    std::lock_guard lifecycleLock(gMeshWorkerLifecycleMutex);
    auto const nextGeneration = gMeshWorkerGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    (void)nextGeneration;
    if (gMeshWorkerExecutor) {
        // stop() drains the executor and joins its sole worker. Tasks never
        // acquire the active projection mutex, so this is safe while state is
        // detached from the active world.
        gMeshWorkerExecutor->stop();
        gMeshWorkerExecutor.reset();
    }
    gMeshWorkerBusy.store(false, std::memory_order_release);
    gMeshWorkerFatalFailure.store(false, std::memory_order_release);
    std::lock_guard lock(gMeshWorkerMutex);
    gCompletedSectionBuilds.clear();
    gCancelledSectionBuilds.clear();
}

bool submitMeshWorkerTask(
    std::uint64_t workerGeneration,
    std::function<AsyncSectionBuildResult()> task
) {
    auto admitted=resourcepacks::state().enter(resourcepacks::Role::MeshJob);
    if(!admitted)return false;
    auto reloadLease=std::make_shared<resourcepacks::State::Lease>(std::move(admitted));
    struct OwnedTask {
        std::shared_ptr<resourcepacks::State::Lease> lease;
        std::function<AsyncSectionBuildResult()> task;
        // Explicit member order destroys native task captures before the lease.
    };
    auto workload=std::make_shared<OwnedTask>(OwnedTask{reloadLease,std::move(task)});
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
        auto const admitted = gMeshWorkerExecutor->submit([
            workload,
            workerGeneration
        ]() mutable noexcept {
            runLeasedWorkerTaskBoundary(
                gMeshWorkerBusy,
                workload->task,
                [&](AsyncSectionBuildResult result) {
                    bool const published=resourcepacks::state().publishIfCurrent(*workload->lease,[&] {
                        if(workerGeneration!=gMeshWorkerGeneration.load(std::memory_order_acquire))return false;
                        std::lock_guard lock(gMeshWorkerMutex);
                        gCompletedSectionBuilds.emplace_back(std::move(result));return true;
                    });
                    if(!published && workerGeneration==gMeshWorkerGeneration.load(std::memory_order_acquire)) {
                        // No native payload crosses Pause. Retain only the
                        // cancelled ticket so before-native cancel/resume can
                        // release buildInFlight on the normal render owner.
                        std::lock_guard lock(gMeshWorkerMutex);
                        gCancelledSectionBuilds.push_back({result.workerGeneration,result.structureGeneration,result.revision,result.section});
                    }
                },
                [workerGeneration]() noexcept {
                    if (workerGeneration == gMeshWorkerGeneration.load(std::memory_order_acquire)) {
                        gMeshWorkerFatalFailure.store(true, std::memory_order_release);
                    }
                }
            );
        });
        if (!admitted) {
            gMeshWorkerBusy.store(false, std::memory_order_release);
            return false;
        }
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
std::vector<CancelledSectionBuild> takeCancelledSectionBuilds() {
    std::lock_guard lock(gMeshWorkerMutex);auto result=std::move(gCancelledSectionBuilds);gCancelledSectionBuilds.clear();return result;
}

bool meshWorkerIsBusy() {
    return gMeshWorkerBusy.load(std::memory_order_acquire);
}

bool consumeMeshWorkerFatalFailure() {
    return gMeshWorkerFatalFailure.exchange(false, std::memory_order_acq_rel);
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
