#include "SharedTypes.h"

#include <cstdio>
#include <random>

namespace lholo::sync {
namespace {

[[nodiscard]] constexpr int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

bool Uuid::isNil() const noexcept {
    for (auto byte : bytes) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

std::string Uuid::toString() const {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(kUuidStringLength);
    for (std::size_t i = 0; i < kUuidBytes; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out.push_back('-');
        }
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0x0f]);
    }
    return out;
}

bool Uuid::parse(std::string_view text, Uuid& out) noexcept {
    if (text.size() != kUuidStringLength) {
        return false;
    }
    std::size_t index = 0;
    for (std::size_t i = 0; i < kUuidBytes; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            if (text[index] != '-') {
                return false;
            }
            ++index;
        }
        int const high = hexValue(text[index]);
        int const low = hexValue(text[index + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
        index += 2;
    }
    return index == text.size();
}

Uuid Uuid::random() {
    Uuid result;
    std::random_device device;
    for (std::size_t i = 0; i < kUuidBytes; i += 4) {
        std::uint32_t const value = device();
        result.bytes[i + 0] = static_cast<std::uint8_t>(value & 0xff);
        result.bytes[i + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
        result.bytes[i + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
        result.bytes[i + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
    }
    // RFC 4122 version 4 / variant bits so the value is a well-formed v4 UUID.
    result.bytes[6] = static_cast<std::uint8_t>((result.bytes[6] & 0x0f) | 0x40);
    result.bytes[8] = static_cast<std::uint8_t>((result.bytes[8] & 0x3f) | 0x80);
    return result;
}

std::string Sha256Hash::toHex() const {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(kSha256HexLength);
    for (auto byte : bytes) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0f]);
    }
    return out;
}

bool Sha256Hash::parse(std::string_view hex, Sha256Hash& out) noexcept {
    if (hex.size() != kSha256HexLength) {
        return false;
    }
    for (std::size_t i = 0; i < kSha256Bytes; ++i) {
        int const high = hexValue(hex[i * 2]);
        int const low = hexValue(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

bool Sha256Hash::isZero() const noexcept {
    for (auto byte : bytes) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

} // namespace lholo::sync
