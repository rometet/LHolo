#pragma once
#include <cstdint>
#include <memory>
class LocalPlayer;class BlockPos;class ItemStack;class ComplexInventoryTransaction;
namespace lholo::place::detail {
// Admission is not a send/ack. The caller leaves its Manual request pending.
void queueRotationPlacement(LocalPlayer&,BlockPos const& cell,std::int64_t cellKey,
    unsigned ghostId,int slot,ItemStack const&,float yaw,std::unique_ptr<ComplexInventoryTransaction>);
bool installRotationDeliveryHook();
bool uninstallRotationDeliveryHook();
}
