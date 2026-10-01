#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace lholo::structure::detail {

struct NbtPayloadRange { std::size_t begin{}, end{}; };

// Validate little-endian binary NBT without constructing its generic tree.
// Locate the index payload by grammar/path, never by matching bytes inside a
// string or array. Primitive lists/arrays are skipped in one bounded operation.
class BedrockNbtScanner {
public:
    explicit BedrockNbtScanner(std::string_view bytes, std::size_t nodeBytes = 64,
                               std::size_t decodedLimit = 1024ULL * 1024ULL * 1024ULL,
                               bool primitiveIndices = true)
        : mBytes(bytes), mNodeBytes(nodeBytes), mDecodedLimit(decodedLimit), mPrimitiveIndices(primitiveIndices) {}

    std::optional<NbtPayloadRange> scan() {
        if (u8() != 10) fail("root is not a compound");
        (void)name();
        payload(10, 0, 1);
        if (mOffset != mBytes.size()) fail("trailing bytes");
        return mIndices;
    }

private:
    std::string_view mBytes;
    std::size_t mOffset{}, mNames{};
    std::size_t mDecodedBytes{}, mNodeBytes, mDecodedLimit;
    bool mPrimitiveIndices;
    std::optional<NbtPayloadRange> mIndices;
    [[noreturn]] static void fail(char const* reason) {
        throw std::runtime_error(std::string{"Invalid Bedrock NBT: "} + reason);
    }
    void skip(std::size_t bytes) {
        if (bytes > mBytes.size() - mOffset) fail("truncated payload");
        mOffset += bytes;
    }
    void charge(std::size_t count, std::size_t width) {
        if (!width || count > (mDecodedLimit - mDecodedBytes) / width) fail("decoded NBT exceeds memory budget");
        mDecodedBytes += count * width;
    }
    std::uint8_t u8() {
        auto const begin = mOffset;
        skip(1);
        return static_cast<std::uint8_t>(mBytes[begin]);
    }
    std::uint32_t unsignedLE(unsigned bytes) {
        std::uint32_t value{};
        for (unsigned index = 0; index < bytes; ++index) value |= static_cast<std::uint32_t>(u8()) << (index * 8);
        return value;
    }
    std::string_view name() {
        auto const size = unsignedLE(2);
        auto const begin = mOffset;
        skip(size);
        charge(size, 1);
        return mBytes.substr(begin, size);
    }
    std::size_t count() {
        auto const raw = unsignedLE(4);
        if (raw > 0x7fffffffU) fail("negative array/list length");
        return raw;
    }
    void elements(std::size_t size, std::size_t width) {
        if (size > (mBytes.size() - mOffset) / width) fail("truncated array/list");
        skip(size * width);
    }
    void payload(std::uint8_t type, std::size_t depth, unsigned path = 0) {
        if (depth > 128) fail("nesting exceeds 128");
        constexpr std::size_t minimum[]{0, 1, 2, 4, 8, 4, 8, 4, 2, 5, 1, 4, 4};
        if (!type || type > 12) fail("unknown tag type");
        if (type <= 6) { skip(minimum[type]); return; }
        if (type == 8) { (void)name(); return; }
        if (type == 7 || type == 11 || type == 12) {
            auto const size = count();
            charge(size, type == 7 ? 1 : type == 11 ? 4 : 8);
            elements(size, type == 7 ? 1 : type == 11 ? 4 : 8);
            return;
        }
        if (type == 9) {
            auto const elementType = u8();
            auto const size = count();
            if (elementType > 12 || (!elementType && size)) fail("invalid list element type");
            if (!size) return;
            if (size > (mBytes.size() - mOffset) / minimum[elementType]) fail("truncated list");
            // The optimized block-index path stores List<Int> as primitive
            // ints, while generic native lists own one Tag per element.
            // Preserve large valid index layers without permitting a tiny
            // metadata scalar list to expand into gigabytes of native nodes.
            charge(size, mPrimitiveIndices && path == 3 && elementType == 3 ? 4 : mNodeBytes);
            if (elementType <= 6) { elements(size, minimum[elementType]); return; }
            for (std::size_t index = 0; index < size; ++index) payload(elementType, depth + 1, path == 3 ? 3 : 0);
            return;
        }
        std::unordered_set<std::string_view> keys;
        for (;;) {
            auto const childType = u8();
            if (!childType) break;
            auto const childName = name();
            // Bound validation-node storage independently of file-controlled
            // element counts. Names themselves are views into the input.
            if (++mNames > (1024ULL * 1024ULL * 1024ULL) / 64) fail("too many compound fields");
            charge(1, mNodeBytes + 2 * sizeof(void*));
            if (!keys.insert(childName).second) fail("duplicate compound key");
            auto const begin = mOffset;
            auto const isIndices = path == 2 && childName == "block_indices" && childType == 9;
            auto const childPath = isIndices ? 3U : path == 1 && childName == "structure" && childType == 10 ? 2U : 0U;
            payload(childType, depth + 1, childPath);
            if (isIndices) mIndices = NbtPayloadRange{begin, mOffset};
        }
    }
};

} // namespace lholo::structure::detail
