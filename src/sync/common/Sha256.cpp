#include "Sha256.h"

#include <cstring>

namespace lholo::sync::sha256 {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t rotr(std::uint32_t value, unsigned bits) noexcept {
    return (value >> bits) | (value << (32 - bits));
}

} // namespace

void Hasher::reset() noexcept {
    mState[0] = 0x6a09e667u;
    mState[1] = 0xbb67ae85u;
    mState[2] = 0x3c6ef372u;
    mState[3] = 0xa54ff53au;
    mState[4] = 0x510e527fu;
    mState[5] = 0x9b05688cu;
    mState[6] = 0x1f83d9abu;
    mState[7] = 0x5be0cd19u;
    mBitLength = 0;
    mBufferLength = 0;
    std::memset(mBuffer, 0, sizeof(mBuffer));
}

void Hasher::processBlock(std::uint8_t const* block) noexcept {
    std::uint32_t w[64]{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24)
             | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
             | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
             | (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (std::size_t i = 16; i < 64; ++i) {
        std::uint32_t const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = mState[0], b = mState[1], c = mState[2], d = mState[3];
    std::uint32_t e = mState[4], f = mState[5], g = mState[6], h = mState[7];

    for (std::size_t i = 0; i < 64; ++i) {
        std::uint32_t const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        std::uint32_t const ch = (e & f) ^ (~e & g);
        std::uint32_t const temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        std::uint32_t const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        std::uint32_t const maj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t const temp2 = s0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    mState[0] += a; mState[1] += b; mState[2] += c; mState[3] += d;
    mState[4] += e; mState[5] += f; mState[6] += g; mState[7] += h;
}

void Hasher::update(void const* data, std::size_t size) noexcept {
    auto const* bytes = static_cast<std::uint8_t const*>(data);
    mBitLength += static_cast<std::uint64_t>(size) * 8u;

    if (mBufferLength != 0) {
        while (size > 0 && mBufferLength < 64) {
            mBuffer[mBufferLength++] = *bytes++;
            --size;
        }
        if (mBufferLength == 64) {
            processBlock(mBuffer);
            mBufferLength = 0;
        }
    }

    while (size >= 64) {
        processBlock(bytes);
        bytes += 64;
        size -= 64;
    }

    while (size > 0) {
        mBuffer[mBufferLength++] = *bytes++;
        --size;
    }
}

Sha256Hash Hasher::finish() noexcept {
    std::uint64_t const bitLength = mBitLength;

    // Padding: append 0x80 then zeros until length % 64 == 56, then 64-bit length.
    std::uint8_t const pad = 0x80;
    update(&pad, 1);
    std::uint8_t const zero = 0x00;
    while (mBufferLength != 56) {
        update(&zero, 1);
    }
    std::uint8_t lengthBytes[8];
    for (std::size_t i = 0; i < 8; ++i) {
        lengthBytes[i] = static_cast<std::uint8_t>(bitLength >> (56 - i * 8));
    }
    // Feed the length without counting it in mBitLength semantics (already final).
    std::memcpy(mBuffer + mBufferLength, lengthBytes, 8);
    processBlock(mBuffer);
    mBufferLength = 0;

    Sha256Hash result;
    for (std::size_t i = 0; i < 8; ++i) {
        result.bytes[i * 4 + 0] = static_cast<std::uint8_t>(mState[i] >> 24);
        result.bytes[i * 4 + 1] = static_cast<std::uint8_t>(mState[i] >> 16);
        result.bytes[i * 4 + 2] = static_cast<std::uint8_t>(mState[i] >> 8);
        result.bytes[i * 4 + 3] = static_cast<std::uint8_t>(mState[i]);
    }
    return result;
}

Sha256Hash hash(void const* data, std::size_t size) noexcept {
    Hasher hasher;
    hasher.update(data, size);
    return hasher.finish();
}

Sha256Hash hashString(std::string_view text) noexcept {
    return hash(text.data(), text.size());
}

} // namespace lholo::sync::sha256
