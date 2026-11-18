// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Collection boundary for real-world block and subchunk notifications. This
// module queues facts only; downstream projection stages decide how to apply
// them.

#pragma once

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/correction/CorrectionWorkQueues.h"
#include "projection/runtime/WorldEventInterest.h"

#include <cstddef>
#include <mutex>
#include <vector>

class BlockSource;
class Level;

namespace lholo::projection::detail {

struct ProjectionPendingEventCounts {
    std::size_t cells{}, sections{}, loadedSections{};
};

void attachProjectionWorldEvents(Level& level, BlockSource& blockSource);
void publishProjectionWorldEventInterest(WorldEventInterest interest);
// A dimension switch invalidates the BlockSource but not the Level. Keep the
// Level listener attached so a later world exit still reaches normal cleanup.
void detachProjectionDimensionEvents();
void detachProjectionWorldEvents();
// State owner takes this after the projection-state mutex around every native
// world access. Listener retirement takes only this and the worker barrier.
// Reentrancy is needed for render failure cleanup/detach on the same thread.
std::recursive_mutex& projectionWorldLifecycleMutex();
bool consumeWorldExitRequest();
bool projectionDimensionSourceDestroyed();
bool projectionWorldEventsFailed();
// Called with the lifecycle barrier held when a consumer cannot finish applying
// queued facts. The existing failure status requires an explicit session reload.
void markProjectionWorldEventsFailed() noexcept;

std::vector<CorrectionEventBatch> takePendingBlockChangeSections(std::size_t limit);
ProjectionPendingEventCounts pendingProjectionEventCounts();
std::vector<SubChunkKey> takePendingLoadedSubChunks(std::size_t limit);

} // namespace lholo::projection::detail
