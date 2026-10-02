#pragma once
#include <functional>
#include <utility>

namespace lholo::structure {
// Testable dispatch contract; block-state orientation remains owned by the
// game's transformer, including stairs, hopper, trapdoor and redstone states.
template<class Block, class Rotation, class Mirror, class NativeTransform>
Block const* nativeBlockTransform(Block const* block, bool identity, Rotation rotation, Mirror mirror, NativeTransform&& transform) {
    if (!block || identity) return block;
    return std::invoke(std::forward<NativeTransform>(transform),*block,rotation,mirror);
}
}
