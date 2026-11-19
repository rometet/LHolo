// LHolo - accidental shared-placement mutation protection (client UI only).
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "ui/SharedEditViewState.h"

namespace lholo::ui {

// This UI guard is deliberately independent of server edit leases.
// Locked by default, reset for each world/session and after each mutation.
struct SharedMutationGuard {
    bool unlocked{false};
    std::uint64_t sessionEpoch{std::numeric_limits<std::uint64_t>::max()};
    std::string pendingCommitToken;

    void lock() {
        unlocked = false;
        pendingCommitToken.clear();
    }
    void unlock() {
        unlocked = true;
        pendingCommitToken.clear();
    }
    void resetForSession(std::uint64_t epoch) {
        if (sessionEpoch == epoch) return;
        sessionEpoch = epoch;
        lock();
    }

    [[nodiscard]] static std::string commitToken(
        sync::Uuid const& id, SharedEditViewState const& draft
    ) {
        // Include all editable fields and the complete placement ID so that
        // changing any field always requires a fresh confirmation click.
        return id.toString() + "|" + std::to_string(draft.name.size()) + ":" + draft.name
            + "|" + std::to_string(draft.x) + "|" + std::to_string(draft.y)
            + "|" + std::to_string(draft.z)
            + "|" + std::to_string(draft.rotation)
            + "|" + std::to_string(draft.mirror);
    }

    [[nodiscard]] bool commitArmed(sync::Uuid const& id, SharedEditViewState const& draft) const {
        return unlocked && !pendingCommitToken.empty() && pendingCommitToken == commitToken(id, draft);
    }

    // First click prepares the exact edit; the second, with unchanged fields,
    // approves it. After approval, the UI re-locks immediately.
    [[nodiscard]] bool approveCommit(sync::Uuid const& id, SharedEditViewState const& draft) {
        if (!unlocked) return false;
        auto token = commitToken(id, draft);
        if (pendingCommitToken != token) {
            pendingCommitToken = std::move(token);
            return false;
        }
        lock();
        return true;
    }
};

} // namespace lholo::ui
