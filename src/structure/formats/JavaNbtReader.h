#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace lholo::structure::detail {

inline constexpr std::size_t kMaximumJavaNbtDecodedBytes = 1024ULL * 1024ULL * 1024ULL;

// Java DataInput strings encode UTF-16 code units as modified UTF-8. Convert
// surrogate pairs and C0 80 NUL to canonical UTF-8 before JSON/text consumers.
// Standard four-byte UTF-8 from third-party schematic writers is also accepted.
inline std::string decodeJavaNbtString(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    auto append = [&](std::uint32_t code) {
        if (code < 0x80) output.push_back(static_cast<char>(code));
        else if (code < 0x800) {
            output.push_back(static_cast<char>(0xC0 | (code >> 6)));
            output.push_back(static_cast<char>(0x80 | (code & 63)));
        } else if (code < 0x10000) {
            output.push_back(static_cast<char>(0xE0 | (code >> 12)));
            output.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
            output.push_back(static_cast<char>(0x80 | (code & 63)));
        } else {
            output.push_back(static_cast<char>(0xF0 | (code >> 18)));
            output.push_back(static_cast<char>(0x80 | ((code >> 12) & 63)));
            output.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
            output.push_back(static_cast<char>(0x80 | (code & 63)));
        }
    };
    std::uint32_t highSurrogate{};
    for (std::size_t offset = 0; offset < input.size();) {
        auto const first = static_cast<std::uint8_t>(input[offset++]);
        std::uint32_t code = first;
        int continuation{};
        std::uint32_t minimum{};
        if (first >= 0xC0 && first <= 0xDF) { code = first & 31; continuation = 1; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { code = first & 15; continuation = 2; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { code = first & 7; continuation = 3; minimum = 0x10000; }
        else if (first >= 0x80) throw std::runtime_error("Invalid Java NBT string encoding");
        if (input.size() - offset < static_cast<std::size_t>(continuation)) {
            throw std::runtime_error("Truncated Java NBT string encoding");
        }
        for (int count = 0; count < continuation; ++count) {
            auto const next = static_cast<std::uint8_t>(input[offset++]);
            if ((next & 0xC0) != 0x80) throw std::runtime_error("Invalid Java NBT string continuation");
            code = (code << 6) | (next & 63);
        }
        if ((code < minimum && !(first == 0xC0 && code == 0)) || code > 0x10FFFF) {
            throw std::runtime_error("Invalid Java NBT string code point");
        }
        if (highSurrogate) {
            if (code >= 0xDC00 && code <= 0xDFFF) {
                append(0x10000 + ((highSurrogate - 0xD800) << 10) + code - 0xDC00);
                highSurrogate = 0;
                continue;
            }
            append(0xFFFD); // Java permits unpaired UTF-16 units; use replacement.
            highSurrogate = 0;
        }
        if (code >= 0xD800 && code <= 0xDBFF) highSurrogate = code;
        else append(code >= 0xDC00 && code <= 0xDFFF ? 0xFFFD : code);
    }
    if (highSurrogate) append(0xFFFD);
    return output;
}

struct JavaNbtTag {
    using ByteArray = std::vector<std::uint8_t>;
    using List = std::vector<JavaNbtTag>;
    using Compound = std::unordered_map<std::string, JavaNbtTag>;
    using IntArray = std::vector<std::int32_t>;
    using LongArray = std::vector<std::int64_t>;
    using Value = std::variant<
        std::monostate, std::int8_t, std::int16_t, std::int32_t, std::int64_t, float, double,
        ByteArray, std::string, List, Compound, IntArray, LongArray>;
    Value value;
};

class JavaNbtReader {
public:
    explicit JavaNbtReader(std::string_view bytes, std::size_t decodedLimit = kMaximumJavaNbtDecodedBytes)
        : mBytes(bytes), mDecodedLimit(decodedLimit) {}

    JavaNbtTag::Compound readRoot() {
        auto const type = readU8();
        if (type != 10) throw std::runtime_error("Litematic 根标签不是 Compound");
        (void)readString();
        auto root = readPayload(type);
        if (mOffset != mBytes.size()) throw std::runtime_error("Litematic NBT has trailing data");
        if (!std::holds_alternative<JavaNbtTag::Compound>(root.value)) {
            throw std::runtime_error("Litematic 根标签无效");
        }
        return std::move(std::get<JavaNbtTag::Compound>(root.value));
    }

private:
    std::string_view mBytes;
    std::size_t mOffset{};
    std::size_t mDecodedBytes{};
    std::size_t mDecodedLimit;

    void charge(std::size_t count, std::size_t elementSize = 1) {
        if (count > (mDecodedLimit - mDecodedBytes) / elementSize) {
            throw std::runtime_error("Litematic decoded NBT exceeds memory budget");
        }
        mDecodedBytes += count * elementSize;
    }

    void require(std::size_t count) const {
        if (count > mBytes.size() - std::min(mOffset, mBytes.size())) {
            throw std::runtime_error("Litematic NBT 数据被截断");
        }
    }

    std::uint8_t readU8() {
        require(1);
        return static_cast<std::uint8_t>(mBytes[mOffset++]);
    }

    template <class T>
    T readBigEndian() {
        require(sizeof(T));
        T value{};
        std::memcpy(&value, mBytes.data() + mOffset, sizeof(T));
        mOffset += sizeof(T);
        if constexpr (sizeof(T) > 1) {
            if constexpr (std::endian::native == std::endian::little) {
                auto* first = reinterpret_cast<std::uint8_t*>(&value);
                std::reverse(first, first + sizeof(T));
            }
        }
        return value;
    }

    std::string readString() {
        auto const length = readBigEndian<std::uint16_t>();
        require(length);
        charge(length);
        auto result = decodeJavaNbtString(mBytes.substr(mOffset, length));
        mOffset += length;
        return result;
    }

    std::size_t readArrayLength() {
        auto const length = readBigEndian<std::int32_t>();
        if (length < 0) throw std::runtime_error("Litematic NBT 数组长度为负数");
        return static_cast<std::size_t>(length);
    }

    JavaNbtTag readPayload(std::uint8_t type, std::size_t depth = 0) {
        constexpr std::size_t kMaximumNbtDepth = 128;
        if (depth > kMaximumNbtDepth) {
            throw std::runtime_error("Litematic NBT nesting is too deep");
        }
        JavaNbtTag tag;
        switch (type) {
        case 1: tag.value = static_cast<std::int8_t>(readU8()); break;
        case 2: tag.value = readBigEndian<std::int16_t>(); break;
        case 3: tag.value = readBigEndian<std::int32_t>(); break;
        case 4: tag.value = readBigEndian<std::int64_t>(); break;
        case 5: tag.value = readBigEndian<float>(); break;
        case 6: tag.value = readBigEndian<double>(); break;
        case 7: {
            auto const count = readArrayLength();
            require(count);
            charge(count);
            JavaNbtTag::ByteArray values(count);
            if (count) std::memcpy(values.data(), mBytes.data() + mOffset, count);
            mOffset += count;
            tag.value = std::move(values);
            break;
        }
        case 8: tag.value = readString(); break;
        case 9: {
            auto const elementType = readU8();
            auto const count = readArrayLength();
            constexpr std::size_t minimumBytes[]{0, 1, 2, 4, 8, 4, 8, 4, 2, 5, 1, 4, 4};
            if (elementType > 12 || (elementType == 0 && count != 0)) {
                throw std::runtime_error("Litematic NBT list element type is invalid");
            }
            if (count && count > (mBytes.size() - mOffset) / minimumBytes[elementType]) {
                throw std::runtime_error("Litematic NBT list payload is truncated");
            }
            charge(count, sizeof(JavaNbtTag));
            JavaNbtTag::List values;
            values.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                values.push_back(readPayload(elementType, depth + 1));
            }
            tag.value = std::move(values);
            break;
        }
        case 10: {
            JavaNbtTag::Compound values;
            for (;;) {
                auto const childType = readU8();
                if (childType == 0) break;
                auto name = readString();
                if (values.contains(name)) throw std::runtime_error("Litematic NBT duplicate compound key");
                // Includes the hash node and conservative pointer overhead;
                // nested payloads and their strings are charged separately.
                charge(1, sizeof(JavaNbtTag::Compound::value_type) + 2 * sizeof(void*));
                values.emplace(
                    std::move(name),
                    readPayload(childType, depth + 1)
                );
            }
            tag.value = std::move(values);
            break;
        }
        case 11: {
            auto const count = readArrayLength();
            if (count > (mBytes.size() - mOffset) / sizeof(std::int32_t)) {
                throw std::runtime_error("Litematic IntArray payload is truncated");
            }
            charge(count, sizeof(std::int32_t));
            JavaNbtTag::IntArray values(count);
            for (auto& value : values) value = readBigEndian<std::int32_t>();
            tag.value = std::move(values);
            break;
        }
        case 12: {
            auto const count = readArrayLength();
            if (count > (mBytes.size() - mOffset) / sizeof(std::int64_t)) {
                throw std::runtime_error("Litematic LongArray payload is truncated");
            }
            charge(count, sizeof(std::int64_t));
            JavaNbtTag::LongArray values(count);
            for (auto& value : values) value = readBigEndian<std::int64_t>();
            tag.value = std::move(values);
            break;
        }
        default: throw std::runtime_error("Litematic 包含不支持的 NBT 标签类型");
        }
        return tag;
    }
};

template <class T>
T const* javaValue(JavaNbtTag::Compound const& compound, std::string_view name) {
    auto const found = compound.find(std::string{name});
    if (found == compound.end()) return nullptr;
    return std::get_if<T>(&found->second.value);
}


} // namespace lholo::structure::detail
