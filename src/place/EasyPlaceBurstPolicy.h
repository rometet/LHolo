// LHolo Easy Place burst safety policy (portable, no native dependencies).
#pragma once

#include <algorithm>

namespace lholo::place::detail {

// A multi-packet tick never uses more items than the initially observed
// stack. If server/local sync updates the stack during a send, use the lower
// count rather than trusting stale inventory contents.
[[nodiscard]] constexpr int burstPacketStackCount(
    int initialCount, int observedCount, int alreadySent
) noexcept {
    if (initialCount <= 0 || observedCount <= 0 || alreadySent < 0
        || alreadySent >= initialCount) return 0;
    return (std::min)(observedCount, initialCount - alreadySent);
}

// No batch can change the selected slot, aux or transaction model. Anything
// with explicit native rotation / multi-block side effects must run singly.
[[nodiscard]] constexpr bool burstCanContinue(
    int alreadySent, bool sameSelectedSlot, bool sameItemAux,
    bool simplePlacement, bool nativeRotationRequired
) noexcept {
    return alreadySent == 0
        || (sameSelectedSlot && sameItemAux && simplePlacement
            && !nativeRotationRequired);
}

// A projected upper-layer cell is the user's AIM, not necessarily the next
// cell to submit.  Return the first eligible offset in the column with the
// lowest Y coordinate.  The predicate must reject occupied/pending cells;
// columnDepth=1 preserves the existing single-cell behaviour.
template <class Eligible>
[[nodiscard]] int firstEligibleBottomUpColumnOffset(int columnDepth, Eligible&& eligible) {
    int const depth = (std::clamp)(columnDepth, 1, 4);
    for (int offset = depth - 1; offset >= 0; --offset) {
        if (eligible(offset)) return offset;
    }
    return -1;
}

} // namespace lholo::place::detail
