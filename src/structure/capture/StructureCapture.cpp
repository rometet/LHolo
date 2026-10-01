#include "structure/capture/StructureCapture.h"

#include "i18n/Message.h"
#include "structure/capture/McstructureExporter.h"
#include "structure/capture/CaptureBounds.h"
#include "structure/capture/CaptureRequests.h"
#include "projection/core/ProjectionCoordinateBounds.h"
#include "app/ListenerRetirement.h"

#include <algorithm>
#include <mutex>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelListener.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include "mc/deps/core/math/Vec3.h"

namespace lholo::structure::capture {
namespace {

std::mutex    gMutex;
std::mutex    gLifecycleMutex;
detail::CaptureRequests gRequests;
Draft         gDraft;
i18n::Message gStatus{i18n::TextKey::CaptureStatusNeedTwoPoints};
Level*        gLevel{};
Dimension*    gDimension{};
std::uint64_t gRevision{};
structure::detail::ClientViewState gClientView;

void resetLocked();

// Capture works even without an active projection. Its own level listener
// invalidates cached value state and queued UI requests before world destruction.
class CaptureLevelListener final : public LevelListener {
public:
    void onLevelDestruction(std::string const&) override {
        std::lock_guard lifecycleLock(gLifecycleMutex);
        std::lock_guard lock(gMutex);
        gLevel = nullptr;
        gDimension = nullptr;
        gClientView.invalidate();
        resetLocked();
    }
};
CaptureLevelListener gLevelListener;

struct ClientContext {
    LocalPlayer* player{};
    Level*       level{};
    Dimension*   dimension{};
};

ClientContext currentContext(LocalPlayer& player) {
    return {&player, &player.getLevel(), &player.getDimension()};
}

void resetLocked() {
    gRequests.resetSession();
    gDraft = {};
    gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedTwoPoints};
    ++gRevision;
}

void syncContextLocked(ClientContext const& context) {
    if (!context.player) {
        if (gLevel || gDimension || gDraft.first || gDraft.second) resetLocked();
        gLevel = nullptr;
        gDimension = nullptr;
        return;
    }
    if (gLevel && (gLevel != context.level || gDimension != context.dimension)) resetLocked();
    gLevel = context.level;
    gDimension = context.dimension;
}

Bounds normalizedBounds(Draft const& draft) {
    auto const& first = *draft.first;
    auto const& second = *draft.second;
    return {
        {std::min(first.x, second.x), std::min(first.y, second.y), std::min(first.z, second.z)},
        {std::max(first.x, second.x), std::max(first.y, second.y), std::max(first.z, second.z)}
    };
}

void setStatus(i18n::Message const& status) {
    std::lock_guard lock(gMutex);
    gStatus = status;
}

} // namespace

Snapshot getSnapshot() {
    std::lock_guard lock(gMutex);
    return {gDraft, gLevel != nullptr, i18n::format(gStatus), gRevision};
}

std::optional<Bounds> getBounds() {
    std::lock_guard lock(gMutex);
    if (!gLevel || !gDraft.first || !gDraft.second) return std::nullopt;
    if (!detail::captureBoundsSupported(*gDraft.first, *gDraft.second)) return std::nullopt;
    return normalizedBounds(gDraft);
}

void updateDraft(Draft const& draft, std::uint64_t revision) {
    std::lock_guard lock(gMutex);
    if (!gLevel || revision != gRevision) return;
    if (gDraft == draft) return;
    gDraft = draft;
    if (gDraft.first && gDraft.second) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusSelectionReady};
    } else if (gDraft.first || gDraft.second) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedOtherPoint};
    } else {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedTwoPoints};
    }
    ++gRevision;
}

void setPointFromPlayer(PointSlot slot, std::uint64_t revision) {
    std::lock_guard lock(gMutex);
    if (!gLevel) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusNoWorld};
        return;
    }
    if (revision != gRevision) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusSelectionChanged};
        return;
    }
    gRequests.requestPoint(slot);
}

std::optional<structure::detail::ClientViewSnapshot> getClientViewSnapshot() {
    std::lock_guard lock(gMutex);
    return gClientView.snapshot();
}

void exportStructure(Draft const& draft, std::filesystem::path const& output, std::uint64_t revision) {
    std::lock_guard lock(gMutex);
    if (!gLevel) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusNoWorld};
        return;
    }
    if (draft.mode != CaptureMode::Client) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusSingleplayerUnsupported};
        return;
    }
    if (!draft.first || !draft.second) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusNeedBothPoints};
        return;
    }
    if (revision != gRevision) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusSelectionChanged};
        return;
    }
    if (!detail::captureBoundsSupported(*draft.first, *draft.second)) {
        gStatus = i18n::Message{i18n::TextKey::CaptureStatusBoundsInvalid};
        return;
    }
    gRequests.requestExport(draft, output);
    if (gDraft != draft) { gDraft = draft; ++gRevision; }
    gStatus = i18n::Message{i18n::TextKey::CaptureStatusExportQueued};
}

void tick(LocalPlayer& player) {
    // The native player is borrowed only for the duration of its own tick.
    // Lifecycle -> value-state is also the destruction/shutdown lock order.
    std::lock_guard lifecycleLock(gLifecycleMutex);
    auto const context = currentContext(player);
    Level* previous{};
    {
        std::lock_guard lock(gMutex);
        previous = gLevel;
    }
    if (previous != context.level) {
        app::detachAndRetireListener(previous,
            [](Level& level) { level.removeListener(gLevelListener); },
            [] {
                std::lock_guard lock(gMutex);
                gLevel = nullptr;
                gDimension = nullptr;
                gClientView.invalidate();
                resetLocked();
            });
        context.level->addListener(gLevelListener);
    }

    std::optional<detail::CaptureRequests::Export> request;
    {
        std::lock_guard lock(gMutex);
        syncContextLocked(context);
        auto const forward = player.getViewVector(1.0f);
        gClientView.publish(context.level, context.dimension, player.getRotation().y,
            {forward.x, forward.y, forward.z});
        auto const points = gRequests.takePoints();
        if (points) {
            auto const position = player.getFeetPos();
            auto const cell = projection::detail::checkedBlockCell({position.x, position.y, position.z}, 1);
            if (!cell) {
                gStatus = i18n::Message{i18n::TextKey::CaptureStatusBoundsInvalid};
            } else {
                Point const point{(*cell)[0], (*cell)[1], (*cell)[2]};
                if (points & 1u) gDraft.first = point;
                if (points & 2u) gDraft.second = point;
                gStatus = i18n::Message{points & 2u ? i18n::TextKey::CaptureStatusPoint2Recorded
                                                 : i18n::TextKey::CaptureStatusPoint1Recorded};
                ++gRevision;
            }
        }
        request = gRequests.takeExport();
    }
    if (!request) return;
    auto const& draft = request->draft;
    if (!draft.first || !draft.second || !detail::captureBoundsSupported(*draft.first, *draft.second)) {
        setStatus(i18n::Message{i18n::TextKey::CaptureStatusBoundsInvalid});
        return;
    }
    auto const bounds = normalizedBounds(draft);
    auto status = [&](i18n::TextKey key) {
        std::lock_guard lock(gMutex);
        // Clear/world replacement may cancel a request during a slow export.
        if (gRequests.session() == request->session) gStatus = i18n::Message{key};
    };

    try {
    BlockPos const min{bounds.min.x, bounds.min.y, bounds.min.z};
    BlockPos const max{bounds.max.x, bounds.max.y, bounds.max.z};
    auto& region = player.getDimensionBlockSource();
    if (!region.areChunksFullyLoaded(min, max)) {
        status(i18n::TextKey::CaptureStatusRegionNotLoaded);
        return;
    }

    auto structure = StructureTemplate::create(
        "lholo:client_export",
        region,
        BoundingBox{min, max},
        false,
        !draft.includeEntities
    );
    if (!structure) {
        status(i18n::TextKey::CaptureStatusTemplateFailed);
        return;
    }
    if (!exportMcstructure(*structure, request->output)) {
        status(i18n::TextKey::CaptureStatusWriteFailed);
        return;
    }
    status(i18n::TextKey::CaptureStatusExported);
    } catch (...) {
        status(i18n::TextKey::CaptureStatusExportFailed);
        throw; // The native callback boundary logs the original exception.
    }
}

void clear() {
    std::lock_guard lock(gMutex);
    resetLocked();
}

void shutdown() {
    std::lock_guard lifecycleLock(gLifecycleMutex);
    Level* previous{};
    {
        std::lock_guard lock(gMutex);
        previous = gLevel;
    }
    auto retire = [] {
        std::lock_guard lock(gMutex);
        gLevel = nullptr;
        gDimension = nullptr;
        gClientView.invalidate();
        resetLocked();
    };
    if (previous) {
        app::detachAndRetireListener(previous,
            [](Level& level) { level.removeListener(gLevelListener); }, retire);
    } else {
        retire();
    }
}

} // namespace lholo::structure::capture
