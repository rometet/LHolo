// LHolo Easy Place — distinguish native packet submission from queued input.
// A queued rotation is not a server acknowledgement and must not lock a cell
// until the native PlayerAuthInput packet actually forwards it.
#pragma once

namespace lholo::place::detail {
enum class PlacementDispatchResult {
    Rejected, // no transaction sent or queued
    Sent,     // legacy transaction delivered to packet sender, not server ACK
    Queued    // waiting for fresh native AuthInput; stop other placements
};
[[nodiscard]] constexpr bool consumesPlacementTick(PlacementDispatchResult result) noexcept {
    return result != PlacementDispatchResult::Rejected;
}
[[nodiscard]] constexpr bool isPlacementSent(PlacementDispatchResult result) noexcept {
    return result == PlacementDispatchResult::Sent;
}
}
