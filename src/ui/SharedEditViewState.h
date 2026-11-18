// LHolo - shared-placement edit draft (UI layer)
//
// Owns the transient values a user is changing for one shared placement. It
// lives beside the menu model rather than inside it so the draft survives the
// per-frame model rebuild, and it is never persisted. The sync client owns the
// server lease; this struct only holds what the user typed.

#pragma once

#include <cstdint>
#include <string>

#include "i18n/Translator.h"
#include "sync/common/SharedTypes.h"

namespace lholo::ui {

// The wire DTO accepts four quarter-turns and three mirror axes; the UI clamps
// to those ranges so a commit can never send an out-of-range value.
[[nodiscard]] constexpr int clampSharedRotation(int rotation) noexcept {
    return rotation < 0 ? 0 : (rotation > 3 ? 3 : rotation);
}
[[nodiscard]] constexpr int clampSharedMirror(int mirror) noexcept {
    return mirror < 0 ? 0 : (mirror > 2 ? 2 : mirror);
}

struct SharedEditViewState {
    sync::Uuid placementId{};
    std::string name;
    std::int64_t x{};
    std::int64_t y{};
    std::int64_t z{};
    int rotation{};
    int mirror{};
    bool draftReady{false};

    // Seeds the draft from a catalog entry when the user starts an edit.
    void beginFrom(sync::SharedPlacementDto const& item) {
        placementId = item.placementId;
        name = item.displayName;
        x = item.originX;
        y = item.originY;
        z = item.originZ;
        rotation = clampSharedRotation(item.rotation);
        mirror = clampSharedMirror(item.mirror);
        draftReady = true;
    }

    void clear() { *this = SharedEditViewState{}; }

    [[nodiscard]] bool editing(sync::Uuid const& id) const {
        return draftReady && placementId == id;
    }

    // Applies the draft to a catalog entry, preserving the immutable identity
    // fields (id, hash, format, dimension, revision, owner).
    [[nodiscard]] sync::SharedPlacementDto applyTo(sync::SharedPlacementDto item) const {
        item.displayName = name;
        item.originX = x;
        item.originY = y;
        item.originZ = z;
        item.rotation = clampSharedRotation(rotation);
        item.mirror = clampSharedMirror(mirror);
        return item;
    }
};

// The editor stays open while the request is in flight, while the lease is held
// and after a failure so the reason remains visible; it closes once the edit is
// idle or has been applied.
[[nodiscard]] constexpr bool editPhaseIsActive(sync::EditPhase phase) noexcept {
    return phase == sync::EditPhase::Pending || phase == sync::EditPhase::Granted
        || phase == sync::EditPhase::Committing;
}
[[nodiscard]] constexpr bool editPanelOpen(sync::EditPhase phase) noexcept {
    return editPhaseIsActive(phase) || phase == sync::EditPhase::Failed;
}
// Commit is only valid while the server lease is actually held.
[[nodiscard]] constexpr bool canCommitEdit(sync::EditPhase phase) noexcept {
    return phase == sync::EditPhase::Granted;
}
// The draft is kept only while the editor is open on the same placement; the
// controller applies this every frame so a closed or retargeted edit never
// leaves a stale draft behind.
[[nodiscard]] inline bool keepDraft(SharedEditViewState const& draft, sync::EditPhase phase, sync::Uuid const& placementId) {
    return draft.editing(placementId) && editPanelOpen(phase);
}
// Localized label for the phase; the client's own message carries the detail.
[[nodiscard]] constexpr i18n::TextKey editPhaseTextKey(sync::EditPhase phase) noexcept {
    switch (phase) {
        case sync::EditPhase::Pending: return i18n::TextKey::SharedEditPending;
        case sync::EditPhase::Granted: return i18n::TextKey::SharedEditGranted;
        case sync::EditPhase::Committing: return i18n::TextKey::SharedEditCommitting;
        case sync::EditPhase::Applied: return i18n::TextKey::SharedEditApplied;
        case sync::EditPhase::Failed: return i18n::TextKey::SharedEditFailed;
        case sync::EditPhase::Idle: break;
    }
    return i18n::TextKey::None;
}

} // namespace lholo::ui