#pragma once
#include <cstdint>
class LocalPlayer;
class BlockPos;
class ItemUseInventoryTransaction;
namespace lholo::place::detail {
// Exact, short-lived receipt for one already-admitted native transaction.
// Never writes blocks or the LocalPlayer camera; only the local server actor's
// placement-time rotation may be borrowed while vanilla handles that receipt.
bool rememberLocalPlacementRotation(LocalPlayer const&, BlockPos const& destination,
    ItemUseInventoryTransaction const&, float pitch, float yaw,
    std::uint64_t ownerToken, std::uint64_t ownerEpoch) noexcept;
void cancelLocalPlacementRotations(std::uint64_t ownerToken=0, std::uint64_t ownerEpoch=0) noexcept;
bool installLocalPlacementRotationHook();
bool uninstallLocalPlacementRotationHook();
}
