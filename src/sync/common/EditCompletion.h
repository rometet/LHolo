#pragma once

#include <cstdint>
#include <string_view>

#include "sync/common/SharedTypes.h"

// Decisions that complete an in-flight shared-placement edit (section 4.5).
//
// Completion used to be inferred from a single RESULT matched only by placement
// id, while every edit request was sent with request id 0. That is ambiguous: a
// renewal or another request answered with LOCKED/REVISION_CONFLICT could fail a
// commit the server had in fact applied, and a missed RESULT left the client
// "committing" until the timeout fired even though the update had landed. Both
// the correlation rules and the completion fallback live here so the client
// state machine and its regression tests share one definition.

namespace lholo::sync {

// What the client is currently waiting for, as seen by the completion rules.
struct EditInFlight {
    bool active{false};               // a request is outstanding or a lock is held
    bool committing{false};           // PLACEMENT_UPDATE sent, awaiting confirmation
    bool lockHeld{false};             // the server granted a lease
    Uuid placementId{};               // placement being edited
    std::uint64_t grantRequestId{0};  // outstanding EDIT_BEGIN / EDIT_RENEW
    std::uint64_t commitRequestId{0}; // outstanding PLACEMENT_UPDATE
    std::uint64_t baseRevision{0};    // server revision the draft was based on
};

enum class EditOutcome : std::uint8_t {
    Ignore = 0,  // not this edit's business
    Granted,     // lease granted / renewed
    Applied,     // the update is confirmed on the server
    Failed,      // the request the edit is waiting for was rejected
};

// A reply belongs to the edit only when it answers the request the edit is
// currently waiting for. Starting a commit clears the grant id, so a renewal
// that raced the update can no longer fail (or complete) the commit.
[[nodiscard]] constexpr bool correlatesWithEdit(EditInFlight const& edit, std::uint64_t requestId) noexcept {
    if (!edit.active || requestId == 0) {
        return false;
    }
    return requestId == (edit.committing ? edit.commitRequestId : edit.grantRequestId);
}
// EDIT_GRANTED completes a lock request, or renews a held lease.
[[nodiscard]] constexpr EditOutcome editOutcomeFromGranted(
    EditInFlight const& edit, std::uint64_t requestId, Uuid const& placementId
) noexcept {
    if (edit.committing || !correlatesWithEdit(edit, requestId)) {
        return EditOutcome::Ignore;
    }
    return placementId == edit.placementId ? EditOutcome::Granted : EditOutcome::Ignore;
}

// RESULT confirms the update only when it answers the commit itself and carries
// the edited placement; an "ok" result without a placement id (EDIT_RELEASE)
// and any unrelated create/delete echo are ignored.
[[nodiscard]] constexpr EditOutcome editOutcomeFromResult(
    EditInFlight const& edit,
    std::uint64_t requestId,
    bool hasPlacementId,
    Uuid const& placementId,
    std::string_view status
) noexcept {
    if (!edit.committing || !correlatesWithEdit(edit, requestId)) {
        return EditOutcome::Ignore;
    }
    if (!hasPlacementId || !(placementId == edit.placementId)) {
        return EditOutcome::Ignore;
    }
    return status == "ok" ? EditOutcome::Applied : EditOutcome::Failed;
}

// ERROR fails the edit only for the request the edit is waiting for.
[[nodiscard]] constexpr EditOutcome editOutcomeFromError(EditInFlight const& edit, std::uint64_t requestId) noexcept {
    return correlatesWithEdit(edit, requestId) ? EditOutcome::Failed : EditOutcome::Ignore;
}

// Independent confirmation for a commit whose RESULT never arrived: the server
// broadcasts the applied placement (CATALOG_DELTA "upsert") right after it, so a
// revision past the base revision proves the update landed. Used only as the
// timeout fallback, so a competing editor's delta cannot fake a success while
// this edit is still committing and awaiting its own RESULT.
[[nodiscard]] constexpr bool deltaRevisionConfirmsCommit(EditInFlight const& edit, std::uint64_t revision) noexcept {
    return edit.active && edit.committing && revision > edit.baseRevision;
}

} // namespace lholo::sync