#pragma once
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace lholo::projection::detail {
// Only engine-owned immutable Block state pointers. The cache never stores
// world positions or BlockGraphics, and its lifetime is one mesh build.
template<class Block>
class SectionBlockTransformCache {
public:
    template<class Callback>
    Block const* transformed(Block const* source, Callback&& transform) {
        if (!source) return nullptr;
        auto it = mBlocks.find(source);
        if (it != mBlocks.end()) return it->second;
        auto const* result = std::invoke(std::forward<Callback>(transform), source);
        mBlocks.emplace(source, result);
        return result;
    }
    [[nodiscard]] std::size_t size() const noexcept { return mBlocks.size(); }
private:
    std::unordered_map<Block const*, Block const*> mBlocks;
};
} // namespace lholo::projection::detail
