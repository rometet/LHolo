#pragma once
#include "projection/mesh/SectionBlockTransformCache.h"
#include <array>
#include <cstddef>
#include <vector>

namespace lholo::tests {
template<class Check>
void runSectionBlockTransformCacheChecks(Check check) {
    using lholo::projection::detail::SectionBlockTransformCache;
    struct Dummy { int state; };
    std::array<Dummy, 4> sources{{{1},{2},{3},{4}}};
    std::array<Dummy, 4> rotated{{{10},{20},{30},{40}}};
    SectionBlockTransformCache<Dummy> first;
    std::size_t transforms{};
    auto transform = [&](Dummy const* ptr) -> Dummy const* {
        ++transforms;
        auto const offset = static_cast<std::size_t>(ptr - sources.data());
        return &rotated[offset];
    };
    check(first.transformed(nullptr, transform) == nullptr);
    for (int i = 0; i < 10000; ++i) {
        auto const index = static_cast<std::size_t>(i % 4);
        check(first.transformed(&sources[index], transform) == &rotated[index]);
    }
    check(transforms == 4);
    check(first.size() == 4);
    // The cache must be task-local: a new build applies the new transform
    // settings and is never affected by another task's block orientation.
    SectionBlockTransformCache<Dummy> second;
    auto identity = [](Dummy const* value) { return value; };
    check(second.transformed(&sources[0], identity) == &sources[0]);
    check(second.transformed(&sources[1], identity) == &sources[1]);
    check(second.size() == 2);
}
} // namespace lholo::tests
