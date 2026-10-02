// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/runtime/ProjectionWorldEvents.h"
#include "projection/runtime/CoalescedEventQueue.h"
#include "projection/runtime/ChunkAvailabilityQueue.h"
#include "projection/runtime/EpochFailure.h"
#include "app/NativeCallbackBoundary.h"
#include "app/ListenerRetirement.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <tuple>

#include <Windows.h>

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/BlockSourceListener.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelListener.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"

#include "projection/mesh/ProjectionMeshWorker.h"

namespace lholo::projection::detail {
namespace {

std::mutex                gPendingEventsMutex;
std::recursive_mutex      gWorldLifecycleMutex;
CoalescedEventQueue<SubChunkKey, PendingBlockChange, SubChunkKeyHash> gIncomingBlockChanges;
CoalescedEventQueue<SubChunkKey, SubChunkKey, SubChunkKeyHash> gIncomingLoadedSubChunks;
ChunkAvailabilityQueue gIncomingChunkAvailability;
std::atomic<BlockSource*> gAttachedBlockSource{};
std::atomic<ChunkSource*> gAttachedChunkSource{};
// Identity only: callbacks compare this address and never dereference it.
std::atomic<Dimension*>   gAttachedDimension{};
std::atomic<Level*>       gAttachedLevel{};
std::atomic_bool          gWorldExitPending{};
std::atomic_bool          gDimensionSourceDestroyed{};
EpochFailure             gWorldEventFailure;
WorldEventInterest       gWorldInterest;

class ProjectionBlockSourceListener final : public BlockSourceListener {
public:
    void onSourceDestroyed(BlockSource& source) override {
        // Worker-local BlockSources are unrelated. Do not make their teardown
        // wait on the render owner's barrier while that owner joins the worker.
        if (gAttachedBlockSource.load(std::memory_order_acquire) != &source) return;
        std::lock_guard lifecycleLock(gWorldLifecycleMutex);
        auto* expected = &source;
        if (gAttachedBlockSource.compare_exchange_strong(
                expected,
                nullptr,
                std::memory_order_acq_rel
            )) {
            gAttachedChunkSource.store(nullptr, std::memory_order_release);
            gAttachedDimension.store(nullptr, std::memory_order_release);
            gDimensionSourceDestroyed.store(true, std::memory_order_release);
            // A standalone dimension/source destruction can precede the
            // Level event. Workers retain that Dimension and chunk view too.
            stopMeshWorker();
        }
    }

    void onBlockChanged(
        BlockSource& source,
        BlockPos const&              pos,
        uint                           layer,
        Block const&                  block,
        Block const&                  oldBlock,
        int,
        ActorBlockSyncMessage const*,
        BlockChangedEventTarget,
        Actor*
    ) override {
        if (gWorldEventFailure.failed()) return;
        auto const epoch = gWorldEventFailure.token();
        app::invokeNativeCallback([&] {
        // Layer zero is the ordinary solid-block layer. Keep the occurrence
        // time with the fact so delayed consumers never restart the 10-second
        // auto-placement suppression window.
        bool const destroyed = layer == 0 && !oldBlock.isAir() && block.isAir();
        std::lock_guard lock(gPendingEventsMutex);
        // Keep the source check under the same lock as the queue mutation. A
        // callback that started just before world teardown must not append an
        // old-world event after onLevelDestruction() has cleared the queue.
        if (&source != gAttachedBlockSource.load(std::memory_order_acquire)
            || epoch != gWorldEventFailure.token()) return;
        if (!gWorldInterest.contains({pos.x, pos.y, pos.z})) return;
        gIncomingBlockChanges.push(std::tuple{pos.x, pos.y, pos.z}, PendingBlockChange{
            pos, destroyed ? GetTickCount64() : 0,
        }, [](auto& previous, auto const& latest) noexcept {
            previous.destroyedAt = (std::max)(previous.destroyedAt, latest.destroyedAt);
        });
        }, [epoch](char const* reason) noexcept {
            gWorldEventFailure.mark(epoch);
            app::reportNativeCallbackFailure("world block notification; reload projection to retry", reason);
        });
    }
};

ProjectionBlockSourceListener gProjectionBlockSourceListener;

class ProjectionLevelListener final : public LevelListener {
    void queueAvailability(LevelChunk& chunk, ChunkSource* source) {
        if (gWorldEventFailure.failed()) return;
        auto const epoch = gWorldEventFailure.token();
        app::invokeNativeCallback([&] {
            std::lock_guard lock(gPendingEventsMutex);
            if (epoch != gWorldEventFailure.token()
                || &chunk.mDimension != gAttachedDimension.load(std::memory_order_acquire)
                || (source && source != gAttachedChunkSource.load(std::memory_order_acquire))) return;
            auto const& p = chunk.mPosition.get();
            auto const minY = chunk.mMin.get().y, maxY = chunk.mMax.get().y;
            if (gWorldInterest.intersectsChunkColumn(p.x, p.z, minY, maxY))
                gIncomingChunkAvailability.push(p.x, p.z, minY, maxY);
        }, [epoch](char const* reason) noexcept {
            gWorldEventFailure.mark(epoch);
            app::reportNativeCallbackFailure("world chunk availability; reload projection to retry", reason);
        });
    }
public:
    void onChunkUnloaded(LevelChunk& chunk) override { queueAvailability(chunk, nullptr); }
    void onChunkLoaded(ChunkSource& source, LevelChunk& chunk) override { queueAvailability(chunk, &source); }
    void onChunkReloaded(ChunkSource& source, LevelChunk& chunk) override { queueAvailability(chunk, &source); }
    void onSubChunkLoaded(
        ChunkSource& source,
        LevelChunk&  chunk,
        short        absoluteSubChunkIndex,
        bool
    ) override {
        if (gWorldEventFailure.failed()) return;
        auto const epoch = gWorldEventFailure.token();
        app::invokeNativeCallback([&] {
        auto const& chunkPosition = chunk.mPosition.get();
        std::lock_guard lock(gPendingEventsMutex);
        // See onBlockChanged(): source validation and queue insertion must be
        // ordered with teardown's queue clear.
        if (&source != gAttachedChunkSource.load(std::memory_order_acquire)
            || epoch != gWorldEventFailure.token()) return;
        SubChunkKey const key{
            chunkPosition.x,
            static_cast<int>(absoluteSubChunkIndex),
            chunkPosition.z
        };
        if (!gWorldInterest.intersectsSubChunk({chunkPosition.x, static_cast<int>(absoluteSubChunkIndex), chunkPosition.z})) return;
        gIncomingLoadedSubChunks.push(key, key, [](auto&, auto const&) noexcept {});
        }, [epoch](char const* reason) noexcept {
            gWorldEventFailure.mark(epoch);
            app::reportNativeCallbackFailure("world subchunk notification; reload projection to retry", reason);
        });
    }

    void onLevelDestruction(std::string const&) override {
        std::lock_guard lifecycleLock(gWorldLifecycleMutex);
        // Publish before waiting for the worker barrier so the render path
        // stops using the old session while engine teardown is in progress.
        gWorldExitPending.store(true, std::memory_order_release);
        // Worker tasks retain non-owning Level/Dimension/ChunkView pointers.
        // Join them before the engine starts destroying the level; deferring
        // this barrier until Present leaves a use-after-free window.
        stopMeshWorker();
        gAttachedLevel.store(nullptr, std::memory_order_release);
        // The level owns this block source and is already tearing it down. Do
        // not retain or later call removeListener through a dying object.
        gAttachedBlockSource.store(nullptr, std::memory_order_release);
        gAttachedChunkSource.store(nullptr, std::memory_order_release);
        gAttachedDimension.store(nullptr, std::memory_order_release);
        {
            std::lock_guard lock(gPendingEventsMutex);
            gIncomingBlockChanges.clear();
            gIncomingLoadedSubChunks.clear();
            gIncomingChunkAvailability.clear();
            gWorldInterest = WorldEventInterest{};
        }
        // Projection shutdown waits for workers and belongs on the next normal
        // overlay/render frame, outside engine teardown.
    }
};

ProjectionLevelListener gProjectionLevelListener;

} // namespace

void publishProjectionWorldEventInterest(WorldEventInterest interest) {
    std::lock_guard lock(gPendingEventsMutex);
    gWorldInterest = std::move(interest);
}

void attachProjectionWorldEvents(Level& level, BlockSource& blockSource) {
    std::lock_guard lifecycleLock(gWorldLifecycleMutex);
    if (auto* attached = gAttachedBlockSource.load(std::memory_order_acquire);
        attached != &blockSource) {
        auto retire = [] {
            std::lock_guard lock(gPendingEventsMutex);
            gAttachedBlockSource.store(nullptr, std::memory_order_release);
            gAttachedChunkSource.store(nullptr, std::memory_order_release);
            gAttachedDimension.store(nullptr, std::memory_order_release);
            gWorldEventFailure.advance();
            gIncomingBlockChanges.clear();
            gIncomingLoadedSubChunks.clear();
            gIncomingChunkAvailability.clear();
        };
        if (attached) {
            app::detachAndRetireListener(attached,
                [](BlockSource& previous) { previous.removeListener(gProjectionBlockSourceListener); }, retire);
        } else {
            retire();
        }
        blockSource.addListener(gProjectionBlockSourceListener);
        gAttachedBlockSource.store(&blockSource, std::memory_order_release);
    }
    gAttachedChunkSource.store(&blockSource.getChunkSource(), std::memory_order_release);
    gAttachedDimension.store(&blockSource.getDimension(), std::memory_order_release);
    gDimensionSourceDestroyed.store(false, std::memory_order_release);
    if (auto* attached = gAttachedLevel.load(std::memory_order_acquire); attached != &level) {
        app::detachAndRetireListener(attached,
            [](Level& previous) { previous.removeListener(gProjectionLevelListener); },
            [] { gAttachedLevel.store(nullptr, std::memory_order_release); });
        level.addListener(gProjectionLevelListener);
        gAttachedLevel.store(&level, std::memory_order_release);
    }
}

void detachProjectionDimensionEvents() {
    std::lock_guard lifecycleLock(gWorldLifecycleMutex);
    gAttachedChunkSource.store(nullptr, std::memory_order_release);
    gAttachedDimension.store(nullptr, std::memory_order_release);
    app::detachAndRetireListener(gAttachedBlockSource.load(std::memory_order_acquire),
        [](BlockSource& previous) { previous.removeListener(gProjectionBlockSourceListener); },
        [] { gAttachedBlockSource.store(nullptr, std::memory_order_release); });
    std::lock_guard lock(gPendingEventsMutex);
    gWorldEventFailure.advance();
    gIncomingBlockChanges.clear();
    gIncomingLoadedSubChunks.clear();
    gIncomingChunkAvailability.clear();
    gDimensionSourceDestroyed.store(false, std::memory_order_release);
    gWorldInterest = WorldEventInterest{};
}

void detachProjectionWorldEvents() {
    std::lock_guard lifecycleLock(gWorldLifecycleMutex);
    // Keep authoritative Level destruction notification until dimension
    // listeners are removed. A failed source removal must not leave a live
    // source borrow without its world-exit lifetime barrier.
    detachProjectionDimensionEvents();
    app::detachAndRetireListener(gAttachedLevel.load(std::memory_order_acquire),
        [](Level& previous) { previous.removeListener(gProjectionLevelListener); },
        [] { gAttachedLevel.store(nullptr, std::memory_order_release); });
    gWorldExitPending.store(false, std::memory_order_release);
}

std::recursive_mutex& projectionWorldLifecycleMutex() {
    return gWorldLifecycleMutex;
}

bool consumeWorldExitRequest() {
    // Keep the signal sticky until detachProjectionWorldEvents() has completed
    // the world-state cleanup. Both the Present and render paths may observe
    // it; clearing it at the first observer allowed the other path to activate
    // the old StructureSession in the new world.
    return gWorldExitPending.load(std::memory_order_acquire);
}

bool projectionDimensionSourceDestroyed() {
    return gDimensionSourceDestroyed.load(std::memory_order_acquire);
}

bool projectionWorldEventsFailed() { return gWorldEventFailure.failed(); }
void markProjectionWorldEventsFailed() noexcept { gWorldEventFailure.mark(gWorldEventFailure.token()); }

std::vector<PendingBlockChange> takePendingBlockChanges(std::size_t limit) {
    std::vector<PendingBlockChange> changes;
    {
        std::lock_guard lock(gPendingEventsMutex);
        changes = gIncomingBlockChanges.take(limit);
    }
    std::sort(changes.begin(), changes.end(), [](auto const& lhs, auto const& rhs) {
        return std::tie(lhs.position.x, lhs.position.y, lhs.position.z)
            < std::tie(rhs.position.x, rhs.position.y, rhs.position.z);
    });
    return changes;
}

std::vector<SubChunkKey> takePendingLoadedSubChunks(std::size_t limit) {
    std::vector<SubChunkKey> loaded;
    {
        std::lock_guard lock(gPendingEventsMutex);
        loaded = gIncomingLoadedSubChunks.take(limit);
        // Normal block/subchunk facts retain priority. Even a custom tall
        // dimension consumes at most 128 column steps per call.
        auto const availability = gIncomingChunkAvailability.take(limit - loaded.size(), 128,
            [](auto const& cell) { return gWorldInterest.intersectsSubChunk(cell); });
        for (auto const& cell : availability) loaded.emplace_back(cell[0], cell[1], cell[2]);
    }
    return loaded;
}

} // namespace lholo::projection::detail
