#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "SharedTypes.h"

// Minimal, self-contained SHA-256 (FIPS 180-4). Used for content-addressed
// file storage and transfer verification. Kept dependency-free so it can be
// compiled into both the client and the server targets.

namespace lholo::sync::sha256 {

class Hasher {
public:
    Hasher() { reset(); }

    void reset() noexcept;
    void update(void const* data, std::size_t size) noexcept;
    [[nodiscard]] Sha256Hash finish() noexcept;

private:
    void processBlock(std::uint8_t const* block) noexcept;

    std::uint32_t mState[8]{};
    std::uint64_t mBitLength{};
    std::uint8_t mBuffer[64]{};
    std::size_t mBufferLength{};
};

// One-shot helpers.
[[nodiscard]] Sha256Hash hash(void const* data, std::size_t size) noexcept;
[[nodiscard]] Sha256Hash hashString(std::string_view text) noexcept;

} // namespace lholo::sync::sha256
