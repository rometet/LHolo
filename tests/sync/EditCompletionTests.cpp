// LHolo Integrated Sync - shared-placement edit completion rules.
//
// Regression cover for the two-player failure where the initiating client
// reported "edit commit timed out" even though the server had applied and
// broadcast the update. Replies are now correlated with the request the edit is
// waiting for, and a commit whose RESULT was never seen is confirmed from the
// catalog revision the server broadcasts.

#include <cstdint>
#include <cstdio>
#include <string>

#include "sync/common/EditCompletion.h"

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, char const* name) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::fprintf(stderr, "FAIL %s\n", name);
    }
}

lholo::sync::Uuid makeId(std::uint8_t seed) {
    lholo::sync::Uuid id;
    id.bytes[15] = seed;
    return id;
}

// Lock requested, granted reply still outstanding.
lholo::sync::EditInFlight pendingEdit(lholo::sync::Uuid const& placement, std::uint64_t grantId) {
    lholo::sync::EditInFlight edit;
    edit.active = true;
    edit.placementId = placement;
    edit.grantRequestId = grantId;
    edit.baseRevision = 3;
    return edit;
}

// Lease held: renewal replies correlate with the grant id.
lholo::sync::EditInFlight grantedEdit(lholo::sync::Uuid const& placement, std::uint64_t grantId) {
    auto edit = pendingEdit(placement, grantId);
    edit.lockHeld = true;
    return edit;
}

// PLACEMENT_UPDATE sent; the client clears the grant id so replies to an older
// renewal can no longer act on the edit.
lholo::sync::EditInFlight committingEdit(
    lholo::sync::Uuid const& placement, std::uint64_t grantId, std::uint64_t commitId, std::uint64_t baseRevision
) {
    auto edit = grantedEdit(placement, grantId);
    edit.committing = true;
    edit.grantRequestId = 0;
    edit.commitRequestId = commitId;
    edit.baseRevision = baseRevision;
    return edit;
}

} // namespace
namespace {

void testResultCorrelation() {
    using namespace lholo::sync;
    Uuid const id = makeId(1);
    auto const committing = committingEdit(id, 7, 9, 3);
    check(editOutcomeFromResult(committing, 9, true, id, "ok") == EditOutcome::Applied,
        "the commit's own RESULT applies the edit");
    check(editOutcomeFromResult(committing, 7, true, id, "ok") == EditOutcome::Ignore,
        "a RESULT for a superseded request id cannot complete the commit");
    check(editOutcomeFromResult(committing, 0, true, id, "ok") == EditOutcome::Ignore,
        "request id 0 never correlates with an edit");
    check(editOutcomeFromResult(committing, 9, false, Uuid{}, "ok") == EditOutcome::Ignore,
        "an id-less RESULT (EDIT_RELEASE) cannot complete the commit");
    check(editOutcomeFromResult(committing, 9, true, makeId(2), "ok") == EditOutcome::Ignore,
        "a RESULT for another placement cannot complete the commit");
    check(editOutcomeFromResult(committing, 9, true, id, "rejected") == EditOutcome::Failed,
        "a non-ok RESULT fails the edit instead of reporting success");
    check(editOutcomeFromResult(grantedEdit(id, 7), 7, true, id, "ok") == EditOutcome::Ignore,
        "a RESULT received before committing is not a completion");
}

void testGrantCorrelation() {
    using namespace lholo::sync;
    Uuid const id = makeId(3);
    check(editOutcomeFromGranted(pendingEdit(id, 5), 5, id) == EditOutcome::Granted,
        "EDIT_GRANTED correlates with the outstanding lock request");
    check(editOutcomeFromGranted(pendingEdit(id, 5), 4, id) == EditOutcome::Ignore,
        "EDIT_GRANTED for another request id is ignored");
    check(editOutcomeFromGranted(pendingEdit(id, 5), 5, makeId(4)) == EditOutcome::Ignore,
        "EDIT_GRANTED for another placement is ignored");
    check(editOutcomeFromGranted(committingEdit(id, 7, 9, 3), 7, id) == EditOutcome::Ignore,
        "a grant arriving during the commit cannot reopen the lock");
    auto finished = pendingEdit(id, 5);
    finished.active = false;
    check(editOutcomeFromGranted(finished, 5, id) == EditOutcome::Ignore,
        "a grant after the edit finished is ignored");
}
void testErrorCorrelation() {
    using namespace lholo::sync;
    Uuid const id = makeId(5);
    auto const committing = committingEdit(id, 7, 9, 3);
    // The reported failure: a renewal that raced the update is answered LOCKED.
    check(editOutcomeFromError(committing, 7) == EditOutcome::Ignore,
        "a stale renewal error cannot fail a commit in flight");
    check(editOutcomeFromError(committing, 9) == EditOutcome::Failed,
        "the commit's own error fails the edit");
    check(editOutcomeFromError(committing, 0) == EditOutcome::Ignore,
        "an uncorrelated error (malformed frame) is ignored");
    check(editOutcomeFromError(pendingEdit(id, 5), 5) == EditOutcome::Failed,
        "an error answering the lock request fails the edit");
}

void testCommitTimeoutFallback() {
    using namespace lholo::sync;
    Uuid const id = makeId(6);
    auto const committing = committingEdit(id, 7, 9, 3);
    check(deltaRevisionConfirmsCommit(committing, 4),
        "a newer catalog revision confirms a commit whose RESULT was never seen");
    check(!deltaRevisionConfirmsCommit(committing, 3),
        "the unchanged base revision does not confirm the commit");
    check(!deltaRevisionConfirmsCommit(committing, 0),
        "an unknown placement revision does not confirm the commit");
    check(!deltaRevisionConfirmsCommit(grantedEdit(id, 7), 4),
        "a held lease without an update is never confirmed by the catalog");
    auto finished = committing;
    finished.active = false;
    check(!deltaRevisionConfirmsCommit(finished, 4),
        "a finished edit is not re-confirmed by the catalog");
}

} // namespace

int main() {
    testResultCorrelation();
    testGrantCorrelation();
    testErrorCorrelation();
    testCommitTimeoutFallback();
    std::printf("LHoloSyncEditCompletionTests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}