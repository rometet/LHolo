#pragma once
#include <cstdint>
#include <memory>
class LocalPlayer;class BlockPos;class ItemStack;class ComplexInventoryTransaction;
namespace lholo::place::detail {
// Admission is not a send/ack. The caller leaves its Manual request pending.
[[nodiscard]] bool queueRotationPlacement(LocalPlayer&,BlockPos const& cell,std::int64_t cellKey,
    unsigned ghostId,int slot,ItemStack const&,float pitch,float yaw,
    std::unique_ptr<ComplexInventoryTransaction>,bool requireNativeRotationSync=false,
    bool requireStairWarmup=false);
[[nodiscard]] bool queueExternalRotationPlacement(LocalPlayer&,BlockPos const& cell,
    BlockPos const& support,unsigned targetBlockId,unsigned char face,int slot,ItemStack const&,
    float pitch,float yaw,std::unique_ptr<ComplexInventoryTransaction>,std::uint64_t ownerToken,
    std::uint64_t ownerEpoch,std::uint64_t worldGeneration,int dimension,std::uint32_t flags,
    float maxPickRange);
bool cancelExternalRotationPlacement(std::uint64_t ownerToken,std::uint64_t ownerEpoch) noexcept;
void cancelAllExternalRotationPlacements() noexcept;
bool installRotationDeliveryHook();
bool uninstallRotationDeliveryHook();
}
