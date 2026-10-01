#pragma once

#include "projection/core/LiquidBoundaryMaskCache.h"

namespace lholo::tests {

template <class Check>
void runLiquidBoundaryCacheChecks(Check check) {
    using namespace projection::detail;
    struct Point { float x, y, z; };
    auto face = [](float x, bool positive) {
        std::vector<Point> quad{{x, 0, 0}, {x, 1, 0}, {x, 1, 1}, {x, 0, 1}};
        if (!positive) std::reverse(quad.begin(), quad.end());
        return quad;
    };
    std::array<std::size_t, 3> const ids{10, 4, 30};
    std::array<std::vector<Point>, 3> positions;
    std::array<std::vector<std::uint8_t>, 3> kinds;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        positions[i] = face(static_cast<float>(i), i == 0);
        auto next = face(static_cast<float>(i + 1), true);
        positions[i].insert(positions[i].end(), next.begin(), next.end());
        kinds[i].assign(8, 0U);
    }
    std::fill(kinds[1].begin() + 4, kinds[1].end(), std::uint8_t{1});
    auto assemble = [&](auto const& order, auto const& source) {
        using Value = typename std::decay_t<decltype(source[0])>::value_type;
        std::vector<Value> result;
        for (auto const index : order) result.insert(result.end(), source[index].begin(), source[index].end());
        return result;
    };
    auto countsFor = [&](auto const& order) {
        std::vector<LiquidSectionQuadCount> counts;
        for (auto const index : order) counts.push_back({ids[index], positions[index].size() / 4});
        return counts;
    };
    std::array<std::size_t, 3> order{0, 1, 2};
    auto const initialPositions = assemble(order, positions);
    auto const initialKinds = assemble(order, kinds);
    auto const initialMask = buildNativeLiquidInternalFaceCullMask(std::span<Point const>{initialPositions},
        NativeLiquidFullFaceTolerance, std::span<std::uint8_t const>{initialKinds});
    LiquidBoundaryMaskCache cache;
    check(initialMask.valid && initialMask.facePairs == 1, "cache fixture removes water boundary but keeps water lava interface");
    check(cache.remember(countsFor(order), initialMask), "boundary cache admits complete original section masks");
    do {
        auto const currentPositions = assemble(order, positions);
        auto const currentKinds = assemble(order, kinds);
        auto const expected = buildNativeLiquidInternalFaceCullMask(std::span<Point const>{currentPositions},
            NativeLiquidFullFaceTolerance, std::span<std::uint8_t const>{currentKinds});
        auto const actual = cache.forOrder(countsFor(order));
        check(actual && actual->removeQuads == expected.removeQuads && actual->facePairs == expected.facePairs,
            "every camera order reuses exactly the fresh geometry cull decisions");
        if (!actual) continue;
        // Distinct per-corner payloads model all thirteen vertex streams;
        // compare against an independent gather using fresh geometry decisions.
        for (int stream = 0; stream < 13; ++stream) {
            std::array<std::vector<int>, 3> fields;
            for (std::size_t section = 0; section < fields.size(); ++section) {
                for (int vertex = 0; vertex < 8; ++vertex) {
                    fields[section].push_back(stream * 1000 + static_cast<int>(section) * 100 + vertex);
                }
            }
            auto field = assemble(order, fields);
            std::vector<int> gathered;
            for (std::size_t i = 0; i < field.size(); ++i) {
                if (expected.removeQuads[i / 4] == 0U) gathered.push_back(field[i]);
            }
            check(compactLiquidQuadField(field, actual->removeQuads, 4) && field == gathered,
                "cached cull preserves typed vertex payload and camera section order");
        }
        std::vector<int> quads;
        for (auto const index : order) { quads.push_back(static_cast<int>(index * 2)); quads.push_back(static_cast<int>(index * 2 + 1)); }
        auto gathered = quads;
        gathered.clear();
        for (std::size_t i = 0; i < quads.size(); ++i) if (expected.removeQuads[i] == 0) gathered.push_back(quads[i]);
        check(compactLiquidQuadField(quads, actual->removeQuads, 1) && quads == gathered,
            "cached cull preserves per quad metadata order");
    } while (std::next_permutation(order.begin(), order.end()));

    auto counts = countsFor(order);
    counts[0].quads += 1;
    check(!cache.forOrder(counts), "changed source quad count cannot reuse old mask");
    counts = countsFor(order); counts.pop_back();
    check(!cache.forOrder(counts), "changed visible section subset needs a fresh boundary decision");
    counts = countsFor(order); counts[1] = counts[0];
    check(!cache.forOrder(counts), "duplicate section order cannot reuse another section mask");
    auto invalid = initialMask; invalid.facePairs += 1;
    check(!cache.remember(countsFor(order), invalid) && cache.forOrder(countsFor(order)),
        "invalid cache replacement preserves preceding complete mask");
    check(!cache.remember(counts, initialMask), "duplicate cache source IDs rejected");
    std::vector<int> badLayout{1, 2, 3};
    auto const unchanged = badLayout;
    check(!compactLiquidQuadField(badLayout, initialMask.removeQuads, 4) && badLayout == unchanged,
        "inconsistent typed stream is left intact");
    std::vector<int> empty;
    check(compactLiquidQuadField(empty, initialMask.removeQuads, 4) && empty.empty(), "empty optional stream remains empty");
    cache.clear();
    check(!cache.forOrder(countsFor(order)), "mesh invalidation retires old cull decisions");
}

} // namespace lholo::tests
