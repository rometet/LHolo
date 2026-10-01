#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace lholo::projection::detail {

enum class PraxisLiquidMaterialCandidate : std::uint8_t { SignText, BlendBlock };

[[nodiscard]] constexpr bool liquidReplayMaterialReady(
    PraxisLiquidMaterialCandidate candidate, bool signTextReady, bool blendReady
) noexcept {
    return candidate == PraxisLiquidMaterialCandidate::BlendBlock ? blendReady : signTextReady;
}

[[nodiscard]] constexpr std::optional<PraxisLiquidMaterialCandidate> retainedLiquidMaterial(
    bool signTextReady, bool blendReady
) noexcept {
    if (signTextReady) return PraxisLiquidMaterialCandidate::SignText;
    if (blendReady) return PraxisLiquidMaterialCandidate::BlendBlock;
    return std::nullopt;
}

[[nodiscard]] constexpr bool replayVertexCountFits(std::size_t count) noexcept {
    return count <= static_cast<std::size_t>((std::numeric_limits<int>::max)());
}

struct ReplayCounts {
    std::uint32_t vertices{};
    std::uint32_t capacity{};
};

[[nodiscard]] constexpr std::optional<ReplayCounts> combineReplayCounts(
    std::size_t destination, std::size_t source,
    std::uint32_t destinationCapacity, std::uint32_t sourceCapacity
) noexcept {
    if (!replayVertexCountFits(destination) || !replayVertexCountFits(source)
        || source > static_cast<std::size_t>((std::numeric_limits<int>::max)()) - destination) {
        return std::nullopt;
    }
    auto const vertices = static_cast<std::uint32_t>(destination + source);
    auto const capacity = static_cast<std::uint64_t>(destinationCapacity) + sourceCapacity;
    if (capacity > (std::numeric_limits<std::uint32_t>::max)()) return std::nullopt;
    return ReplayCounts{vertices, static_cast<std::uint32_t>(capacity < vertices ? vertices : capacity)};
}

} // namespace lholo::projection::detail
