#pragma once
#include <cstddef>
#include <cstdint>
#include <tuple>

namespace lholo::projection::detail {
using SubChunkKey = std::tuple<int, int, int>;
struct SubChunkKeyHash {
    std::size_t operator()(SubChunkKey const& key) const noexcept {
        auto const [x, y, z] = key;
        auto mix = [](std::uint64_t value) {
            value ^= value >> 30U;
            value *= 0xBF58476D1CE4E5B9ULL;
            value ^= value >> 27U;
            value *= 0x94D049BB133111EBULL;
            value ^= value >> 31U;
            return value;
        };
        auto hash = mix(static_cast<std::uint32_t>(x));
        hash ^= mix(static_cast<std::uint32_t>(y) + 0x9E3779B9U);
        hash ^= mix(static_cast<std::uint32_t>(z) + 0x85EBCA6BU);
        return static_cast<std::size_t>(hash);
    }
};
} // namespace lholo::projection::detail
