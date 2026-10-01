#pragma once
#include "structure/formats/BedrockNbtScanner.h"
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lholo::structure::detail {

struct StrippedMcstructureIndices {
    std::string                              parseBytes;
    std::vector<std::vector<std::int32_t>>  layers;
};

inline bool validPaletteIndex(std::int32_t index, std::size_t paletteSize) {
    return index == -1 || (index >= 0 && static_cast<std::size_t>(index) < paletteSize);
}

inline std::optional<StrippedMcstructureIndices> stripMcstructureBlockIndices(
    std::string const& bytes, std::size_t nativeNodeBytes = 64
) {
    // Pull the huge block_indices payload out of the generic NBT tree entirely.
    // The remaining NBT (palette, block_position_data, size, metadata) stays
    // byte-for-byte unchanged.
    auto const field = BedrockNbtScanner{bytes, nativeNodeBytes}.scan();
    if (!field) return std::nullopt;
    auto const payload = field->begin;
    if (payload + 5 > bytes.size()) return std::nullopt;
    auto const readI32 = [&](std::size_t offset) -> std::optional<std::int32_t> {
        if (offset + 4 > bytes.size()) return std::nullopt;
        auto const* data =
            reinterpret_cast<unsigned char const*>(bytes.data() + offset);
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(data[0])
            | (static_cast<std::uint32_t>(data[1]) << 8U)
            | (static_cast<std::uint32_t>(data[2]) << 16U)
            | (static_cast<std::uint32_t>(data[3]) << 24U)
        );
    };

    auto const outerType = static_cast<unsigned char>(bytes[payload]);
    if (outerType != 9U && outerType != 11U) return std::nullopt;
    auto const layerCount = readI32(payload + 1);
    if (!layerCount || *layerCount <= 0 || *layerCount > 2) return std::nullopt;

    StrippedMcstructureIndices result;
    result.layers.resize(static_cast<std::size_t>(*layerCount));

    auto cursor = payload + 5;
    for (int layer = 0; layer < *layerCount; ++layer) {
        std::size_t countOffset{};
        std::size_t dataOffset{};
        if (outerType == 9U) {
            if (cursor + 5 > bytes.size()
                || static_cast<unsigned char>(bytes[cursor]) != 3U) {
                return std::nullopt;
            }
            countOffset = cursor + 1;
            dataOffset = cursor + 5;
        } else {
            if (cursor + 4 > bytes.size()) return std::nullopt;
            countOffset = cursor;
            dataOffset = cursor + 4;
        }

        auto const count = readI32(countOffset);
        if (!count || *count < 0) return std::nullopt;
        auto const dataBytes = static_cast<std::uint64_t>(*count) * 4ULL;
        if (dataBytes > bytes.size()
            || dataOffset + dataBytes > bytes.size()) {
            return std::nullopt;
        }

        auto& values = result.layers[static_cast<std::size_t>(layer)];
        values.resize(static_cast<std::size_t>(*count));
        if (!values.empty()) {
            std::memcpy(values.data(), bytes.data() + dataOffset,
                        static_cast<std::size_t>(dataBytes));
        }
        cursor = dataOffset + static_cast<std::size_t>(dataBytes);
    }
    if (cursor != field->end) return std::nullopt;

    // Keep a valid but empty block_indices tag in the generic parse copy.
    result.parseBytes.reserve(bytes.size() - (cursor - payload) + 5);
    result.parseBytes.append(bytes.data(), payload);
    result.parseBytes.push_back(static_cast<char>(11)); // List<IntArray>
    result.parseBytes.append(4, '\0');                  // zero layers
    result.parseBytes.append(bytes.data() + cursor, bytes.size() - cursor);
    return result;
}


inline bool collectRenderableCandidates(
    std::vector<std::int32_t> const&  layer,
    std::uint64_t                     volume,
    std::uint64_t&                    occupied,
    std::vector<std::uint8_t> const&  airPalette,
    std::vector<std::uint8_t>&        candidateMask
) {
    if (static_cast<std::uint64_t>(layer.size()) != volume
        || candidateMask.size() != static_cast<std::size_t>(volume)) {
        return false;
    }
    occupied = 0;
    for (std::size_t index = 0; index < layer.size(); ++index) {
        auto const paletteIndex = static_cast<int>(layer[index]);
        if (!validPaletteIndex(paletteIndex, airPalette.size())) return false;
        if (paletteIndex < 0) continue;
        ++occupied;
        auto const knownAir = static_cast<std::size_t>(paletteIndex) < airPalette.size()
            && airPalette[static_cast<std::size_t>(paletteIndex)] != 0;
        if (!knownAir) candidateMask[index] = 1;
    }
    return true;
}

} // namespace lholo::structure::detail
