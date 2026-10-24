#pragma once

#include <cstdint>
#include <type_traits>

// Optional Praxis coexistence ABI.  This is intentionally a value-only C ABI:
// no pointers, STL types, native object addresses, or borrowed ownership cross
// the DLL boundary.
namespace lholo::flexible_placement_abi {

inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kVersion2 = 2;

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

enum RequestFlags : std::uint32_t {
    RequestGameplay = 1u << 0,
    RequestFocused = 1u << 1,
    RequestGuiClear = 1u << 2,
    RequestModifiers = 1u << 3,
    RequestAirTarget = 1u << 4,
    RequestRotationSync = 1u << 5,
    RequestRotationMode = 1u << 6,
    RequestOffsetMode = 1u << 7,
    RequestAdjacentMode = 1u << 8,
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

struct FlexiblePlacementRequestV2 {
    std::uint32_t version{kVersion2};
    std::uint32_t flags{};
    std::uint64_t ownerToken{};
    std::uint64_t ownerEpoch{};
    std::uint64_t worldGeneration{};
    std::int32_t dimension{};
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
    float maxPickRange{};
    std::uint32_t reserved2{};
};

static_assert(std::is_standard_layout_v<FlexiblePlacementRequestV2>);
static_assert(sizeof(FlexiblePlacementRequestV2) == 104);

struct FlexiblePlacementStatusV2 {
    std::uint32_t version{kVersion2};
    std::uint32_t statusBits{};
    std::uint64_t ownerToken{};
    std::uint64_t ownerEpoch{};
    std::uint64_t worldGeneration{};
    std::int32_t dimension{};
    std::uint32_t reserved{};
};

static_assert(std::is_standard_layout_v<FlexiblePlacementStatusV2>);
static_assert(sizeof(FlexiblePlacementStatusV2) == 40);

enum OwnerStatusBits : std::uint32_t {
    OwnerFeatureEnabled = 1u << 0,
    OwnerGameplay       = 1u << 1,
    OwnerFocused        = 1u << 2,
    OwnerGuiClear       = 1u << 3,
    OwnerModifiers      = 1u << 4,
    OwnerWorldMatch     = 1u << 5,
    OwnerDimensionMatch = 1u << 6,
    OwnerStackMatch     = 1u << 7,
    OwnerModeMatch      = 1u << 8,
    OwnerTokenMatch     = 1u << 9,
    OwnerEpochMatch     = 1u << 10,
    OwnerLive           = 1u << 31,
};

struct FlexiblePlacementOwnerQueryV2 {
    std::uint32_t version{kVersion2};
    std::uint32_t flags{};
    std::uint64_t ownerToken{};
    std::uint64_t ownerEpoch{};
    std::uint64_t worldGeneration{};
    std::int32_t dimension{};
    std::int32_t selectedSlot{};
    std::int32_t itemId{};
    std::int32_t itemAux{};
    std::uint32_t itemCount{};
    std::uint32_t reserved{};
};

static_assert(std::is_standard_layout_v<FlexiblePlacementOwnerQueryV2>);
static_assert(sizeof(FlexiblePlacementOwnerQueryV2) == 56);

struct FlexiblePlacementOwnerStatusV2 {
    std::uint32_t version{kVersion2};
    std::uint32_t flags{};
    std::uint64_t ownerToken{};
    std::uint64_t ownerEpoch{};
    std::uint64_t worldGeneration{};
    std::int32_t dimension{};
    std::uint32_t modeFlags{};
    std::int32_t selectedSlot{};
    std::int32_t itemId{};
    std::int32_t itemAux{};
    std::uint32_t itemCount{};
    std::uint32_t reserved{};
};

static_assert(std::is_standard_layout_v<FlexiblePlacementOwnerStatusV2>);
static_assert(sizeof(FlexiblePlacementOwnerStatusV2) == 64);

} // namespace lholo::flexible_placement_abi

extern "C" {
__declspec(dllexport) std::uint32_t lholo_flexible_placement_v1_get_status() noexcept;
__declspec(dllexport) std::uint32_t lholo_flexible_placement_v1_queue(
    lholo::flexible_placement_abi::FlexiblePlacementRequest request) noexcept;
__declspec(dllexport) lholo::flexible_placement_abi::FlexiblePlacementStatusV2
lholo_flexible_placement_v2_get_status() noexcept;
__declspec(dllexport) std::uint32_t lholo_flexible_placement_v2_queue(
    lholo::flexible_placement_abi::FlexiblePlacementRequestV2 request) noexcept;
__declspec(dllexport) std::uint32_t lholo_flexible_placement_v2_cancel(
    std::uint64_t ownerToken, std::uint64_t ownerEpoch) noexcept;
}

