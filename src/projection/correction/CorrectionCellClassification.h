// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "projection/correction/CorrectionState.h"
#include "projection/core/ProjectionMissingLayers.h"
#include "structure/MaterialTypePresence.h"

#include <cstdint>

namespace lholo::projection::detail {

// Immutable, engine-independent facts from one world/expected-cell comparison.
// The same precedence and material semantics must hold for uniform-air fast
// paths, unloaded chunks, waterlogged cells, and ordinary native comparisons.
struct CorrectionCellFacts {
    bool ready{};
    bool expectedBody{};
    bool expectedLiquid{};
    bool bodyMissing{};
    bool liquidMissing{};
    bool bodyTypeWrong{};
    bool liquidTypeWrong{};
    bool liquidCellOccupiedBySolid{};
    bool bodyStateMatches{true};
    bool liquidStateMatches{true};
};

struct ClassifiedCorrectionCell {
    CorrectionState correction{CorrectionState::Unknown};
    std::uint8_t materialPresent{};
    std::uint8_t missingLayers{MissingLayerBoth};
};

[[nodiscard]] constexpr ClassifiedCorrectionCell classifyCorrectionCell(CorrectionCellFacts const& facts) noexcept {
    CorrectionState correction = CorrectionState::Correct;
    if (!facts.ready) {
        correction = CorrectionState::Unknown;
    } else if (facts.bodyMissing || facts.liquidMissing) {
        correction = CorrectionState::Missing;
    } else if (facts.bodyTypeWrong || facts.liquidTypeWrong || facts.liquidCellOccupiedBySolid) {
        correction = CorrectionState::WrongType;
    } else if (!facts.bodyStateMatches || !facts.liquidStateMatches) {
        correction = CorrectionState::WrongState;
    }
    auto const materialPresent = structure::materialTypePresentMask(
        facts.ready, facts.expectedBody,
        facts.expectedBody && !facts.bodyMissing && !facts.bodyTypeWrong,
        facts.expectedLiquid,
        facts.expectedLiquid && !facts.liquidMissing && !facts.liquidTypeWrong
            && !facts.liquidCellOccupiedBySolid
    );
    return {correction, materialPresent,
            facts.ready ? projectionMissingLayerMask(facts.bodyMissing, facts.liquidMissing)
                        : MissingLayerBoth};
}

// Error HUD counters exclude hidden cells. Region-mesh reuse must apply the
// same visibility gate as ordinary world correction updates.
[[nodiscard]] constexpr std::uint8_t correctionProgressErrorKind(
    CorrectionState correction, bool visible) noexcept {
    if (!visible) return 0;
    return correction == CorrectionState::WrongType ? 1
         : correction == CorrectionState::WrongState ? 2 : 0;
}

} // namespace lholo::projection::detail
