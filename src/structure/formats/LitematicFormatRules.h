#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace lholo::structure::detail {
inline std::uint32_t packedPaletteIndex(
    std::span<std::int64_t const> values, std::uint64_t index, unsigned bits
) {
    if (bits == 0 || bits > 32 || index > (std::numeric_limits<std::uint64_t>::max)() / bits) {
        throw std::runtime_error("Invalid BlockStates bit offset");
    }
    auto const bitOffset = index * bits;
    auto const arrayIndex = bitOffset >> 6;
    auto const shift = static_cast<unsigned>(bitOffset & 63);
    if (arrayIndex >= values.size()) throw std::runtime_error("BlockStates length is insufficient");
    auto packed = static_cast<std::uint64_t>(values[static_cast<std::size_t>(arrayIndex)]) >> shift;
    if (shift + bits > 64) {
        if (arrayIndex + 1 >= values.size()) throw std::runtime_error("BlockStates crossing word is incomplete");
        packed |= static_cast<std::uint64_t>(values[static_cast<std::size_t>(arrayIndex + 1)]) << (64 - shift);
    }
    auto const mask = (1ull << bits) - 1ull;
    return static_cast<std::uint32_t>(packed & mask);
}

// NBT compounds have no ordering contract. A canonical region-name order
// makes last-nonempty-cell overlap precedence independent of hash layout.
template <class NamedMap>
auto namedEntriesInOrder(NamedMap const& entries) {
    std::vector<typename NamedMap::value_type const*> ordered;
    ordered.reserve(entries.size());
    for (auto const& entry : entries) ordered.push_back(&entry);
    std::sort(ordered.begin(), ordered.end(), [](auto const* left, auto const* right) {
        return left->first < right->first;
    });
    return ordered;
}
} // namespace lholo::structure::detail
