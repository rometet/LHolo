#pragma once
#include "projection/mesh/SectionBlockSnapshot.h"
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace lholo::tests {
template<class Check>
void runSectionSnapshotChecks(Check check) {
    using projection::detail::SectionBlockSnapshot;
    std::vector<std::uint8_t> corrections(8193), actors(8193);
    for (std::size_t i = 0; i < corrections.size(); ++i) {
        corrections[i] = static_cast<std::uint8_t>(i % 5);
        actors[i] = static_cast<std::uint8_t>(i % 2);
    }
    SectionBlockSnapshot snapshot;
    std::uint32_t seed = 12345;
    for (int fixture = 0; fixture < 8; ++fixture) {
        std::vector<std::size_t> indices;
        std::vector<bool> expected(corrections.size());
        for (std::size_t i = 0; i < corrections.size(); ++i) {
            seed = seed * 1664525U + 1013904223U;
            bool const selected = fixture == 0 ? i >= 16 && i < 4096
                : fixture == 1 ? i % 128 < 16
                : fixture == 2 ? i % 16 == 0
                : fixture == 3 ? false
                : (seed % static_cast<std::uint32_t>(fixture + 1)) == 0;
            if (selected) { indices.push_back(i); expected[i] = true; }
        }
        auto const uniqueCount = indices.size();
        if (!indices.empty()) { indices.push_back(indices.front()); indices.push_back(indices.back()); }
        std::reverse(indices.begin(), indices.end());
        snapshot.capture(indices, corrections, actors);
        check(snapshot.size() == uniqueCount);
        check(snapshot.bytes() <= uniqueCount * sizeof(SectionBlockSnapshot::Entry));
        for (std::size_t i = 0; i <= corrections.size(); ++i) {
            auto const* value = snapshot.find(i);
            bool const present = i < expected.size() && expected[i];
            check(value ? present && value->correction == corrections[i] && value->actorRenderer == actors[i] : !present);
        }
        check(!snapshot.find((std::numeric_limits<std::size_t>::max)()));
        // Snapshot owns the bytes; a world correction after capture cannot
        // alter any worker observation or render-actor availability.
        if (uniqueCount) {
            auto const index = indices.front();
            auto const saved = *snapshot.find(index);
            corrections[index] ^= 1;
            actors[index] ^= 1;
            check(snapshot.find(index)->correction == saved.correction && snapshot.find(index)->actorRenderer == saved.actorRenderer);
            corrections[index] ^= 1;
            actors[index] ^= 1;
        }
    }
    std::vector<std::size_t> dense(4096);
    std::iota(dense.begin(), dense.end(), 4096);
    snapshot.capture(dense, corrections, actors);
    check(snapshot.find(4096) && snapshot.find(8191) && !snapshot.find(8192));
    bool rejected = false;
    try { snapshot.capture({0, corrections.size()}, corrections, actors); }
    catch (std::out_of_range const&) { rejected = true; }
    check(rejected);
    rejected = false;
    try { snapshot.capture(dense, corrections, std::vector<std::uint8_t>(4096)); }
    catch (std::out_of_range const&) { rejected = true; }
    check(rejected);
    snapshot.capture({}, corrections, actors);
    check(snapshot.size() == 0 && snapshot.bytes() == 0 && !snapshot.find(0));
}
} // namespace lholo::tests
