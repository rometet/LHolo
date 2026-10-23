#pragma once
#include "projection/ProjectionTypes.h"
#include "structure/Verification.h"

namespace lholo::ui {
[[nodiscard]] inline projection::BuildProgress verifierTallyBuildProgress(
    structure::VerificationTally const& tally
) noexcept {
    projection::BuildProgress result;
    // The verifier scans the currently active placement's visible scope.
    // Unlike the renderer's fast per-block progress, this also combines
    // multiple expected body+liquid layers and scans empty region cells.
    // When a completed report exists, use its identical tally rather than
    // mixing the two independent counters and pretending they agree.
    result.total = tally.total();
    result.placed = tally.correct;
    result.visibleTotal = tally.total();
    result.visiblePlaced = tally.correct;
    result.wrongType = tally.wrongType;
    result.wrongState = tally.wrongState;
    result.extra = tally.extra;
    return result;
}
} // namespace lholo::ui
