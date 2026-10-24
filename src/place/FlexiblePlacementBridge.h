#pragma once

#include <cstdint>
#include <type_traits>

// Optional Praxis coexistence ABI.  This is intentionally a value-only C ABI:
// no pointers, STL types, native object addresses, or borrowed ownership cross
// the DLL boundary.
namespace lholo::flexible_placement_abi {

inline constexpr std::uint32_t kVersion = 1;

enum StatusBits : std::uint32_t {
    EasyPlaceOwnsPlacement = 1u << 0,
    ManualOwnsPlacement    = 1u << 1,
    RangeOwnsPlacement     = 1u << 2,
};

enum QueueResult : std::uint32_t {
    QueueRejected = 0,
    QueueAccepted = 1,
    QueueBusy = 2,
    QueueUnavailable = 3,
};

struct FlexiblePlacementRequest {
    std::uint32_t version{kVersion};
    std::int32_t supportX{};
    std::int32_t supportY{};
    std::int32_t supportZ{};
    std::uint32_t targetBlockId{};
    std::uint32_t face{};
    std::uint32_t reserved{};
    std::int32_t selectedSlot{};
    std::int32_t itemId{};
    std::int32_t itemAux{};
    std::uint32_t itemCount{};
    float relativeHitX{};
    float relativeHitY{};
    float relativeHitZ{};
    float pitch{};
    float yaw{};
};

static_assert(std::is_standard_layout_v<FlexiblePlacementRequest>);
static_assert(sizeof(FlexiblePlacementRequest) == 64);

} // namespace lholo::flexible_placement_abi

extern "C" {
__declspec(dllexport) std::uint32_t lholo_flexible_placement_v1_get_status() noexcept;
__declspec(dllexport) std::uint32_t lholo_flexible_placement_v1_queue(
    lholo::flexible_placement_abi::FlexiblePlacementRequest request) noexcept;
}

