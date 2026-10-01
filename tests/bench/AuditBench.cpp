#include "structure/formats/JavaNbtReader.h"
#include "structure/formats/McstructureIndices.h"
#include "projection/mesh/SectionBlockSnapshot.h"
#include "projection/runtime/CoalescedEventQueue.h"
#include "projection/core/ProjectionCoordinateKey.h"
#include "projection/runtime/WorldEventInterest.h"
#include "projection/mesh/DirtySectionSelection.h"
#include "projection/core/ProjectionLiquidFaceCull.h"
#include "projection/core/LiquidBoundaryMaskCache.h"
#include "block/MaterialKeyCache.h"
#include "block/BlockPlacementRules.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
volatile std::uint64_t checksum{};
template <class Work>
double medianMicros(Work work, int repeats = 31) {
    std::vector<double> samples;
    work();
    for (int repeat = 0; repeat < repeats; ++repeat) {
        auto const started = Clock::now();
        work();
        samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - started).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}
void snapshotBench(std::size_t cells, bool sectionLayout = false) {
    std::vector<std::uint8_t> correction(cells), actors(cells);
    for (std::size_t i = 0; i < cells; ++i) { correction[i] = i % 5; actors[i] = i % 2; }
    std::vector<std::size_t> indices;
    std::vector<std::size_t> queries;
    for (std::size_t section = 0; section < 7; ++section) {
        for (std::size_t cell = 0; cell < 4096; ++cell) {
            std::size_t index = (cells / 8 * section + cell) % cells;
            if (sectionLayout) {
                constexpr int delta[7][3]{{0,0,0},{-16,0,0},{16,0,0},{0,-16,0},{0,16,0},{0,0,-16},{0,0,16}};
                auto const x = static_cast<std::size_t>(16 + delta[section][0]) + cell / 256;
                auto const y = static_cast<std::size_t>(16 + delta[section][1]) + cell / 16 % 16;
                auto const z = static_cast<std::size_t>(16 + delta[section][2]) + cell % 16;
                index = (x * (cells / (128 * 128)) + y) * 128 + z;
            }
            indices.push_back(index);
            // Model repeated center-cell passes and direct neighbor reads.
            // This measures snapshot lookup cost, not native tessellation.
            for (int pass = 0; pass < (sectionLayout && section == 0 ? 8 : 1); ++pass) queries.push_back(index);
        }
    }
    auto const full = medianMicros([&] {
        auto correctionCopy = correction;
        auto actorsCopy = actors;
        std::uint64_t sum{};
        for (auto const index : queries) sum += correctionCopy[index] + actorsCopy[index];
        checksum = sum;
    });
    std::printf("snapshot cells=%zu layout=%s lookups=%zu full_copy_and_neighbor_reads_us=%.3f copied_bytes=%zu\n",
        cells, sectionLayout ? "section_xyz" : "contiguous_strips", queries.size(), full, cells * 2);
    auto const compact = medianMicros([&] {
        lholo::projection::detail::SectionBlockSnapshot snapshot;
        snapshot.capture(indices, correction, actors);
        std::uint64_t sum{};
        for (auto const index : queries) {
            auto const* value = snapshot.find(index);
            if (!value) throw std::runtime_error("snapshot missing neighbor");
            sum += value->correction + value->actorRenderer;
        }
        checksum = sum;
    });
    std::printf("snapshot cells=%zu layout=%s lookups=%zu compact_capture_and_neighbor_reads_us=%.3f stored_bytes=%zu\n",
        cells, sectionLayout ? "section_xyz" : "contiguous_strips", queries.size(), compact,
        indices.size() * sizeof(lholo::projection::detail::SectionBlockSnapshot::Entry));
}
void append32(std::string& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<char>(value >> shift));
}
void nbtBench() {
    constexpr std::uint32_t cells = 1024 * 1024;
    std::string bytes("\x0a\0\0\x0b\0\x01x", 7);
    append32(bytes, cells);
    bytes.resize(bytes.size() + cells * 4, 0);
    bytes.push_back(0);
    auto const parse = medianMicros([&] {
        auto const root = lholo::structure::detail::JavaNbtReader(bytes).readRoot();
        checksum = std::get<lholo::structure::detail::JavaNbtTag::IntArray>(root.at("x").value).size();
    }, 11);
    std::printf("java_intarray cells=%u bytes=%zu parse_us=%.3f\n", cells, bytes.size(), parse);
    std::vector<std::int32_t> layer(cells, 1);
    std::vector<std::uint8_t> air{1, 0}, mask(cells);
    std::uint64_t occupied{};
    auto const candidates = medianMicros([&] {
        std::fill(mask.begin(), mask.end(), 0); occupied = 0;
        auto const ok = lholo::structure::detail::collectRenderableCandidates(layer, cells, occupied, air, mask);
        checksum = ok ? occupied : 0;
    });
    std::printf("mcstructure_candidates cells=%u scan_us=%.3f occupancy=%llu\n",
        cells, candidates, static_cast<unsigned long long>(occupied));
}

void eventBench() {
    constexpr std::size_t input = 200000, distinct = 128;
    using Key = lholo::projection::detail::SubChunkKey;
    using Event = std::pair<Key, std::uint64_t>;
    auto const baseline = medianMicros([&] {
        std::deque<Event> pending;
        for (std::size_t i = 0; i < input; ++i) pending.emplace_back(Key{static_cast<int>(i % distinct), -64, 123}, i + 1);
        std::uint64_t sum{};
        while (!pending.empty()) {
            std::vector<Event> batch;
            auto const count = (std::min)(std::size_t{4096}, pending.size());
            batch.reserve(count);
            for (std::size_t i = 0; i < count; ++i) { batch.push_back(pending.front()); pending.pop_front(); }
            std::sort(batch.begin(), batch.end());
            std::vector<Event> merged;
            merged.reserve(batch.size());
            for (auto const& event : batch) {
                if (!merged.empty() && merged.back().first == event.first) {
                    merged.back().second = (std::max)(merged.back().second, event.second);
                } else merged.push_back(event);
            }
            for (auto const& event : merged) sum += event.second;
        }
        checksum = sum;
    }, 11);
    auto const compact = medianMicros([&] {
        lholo::projection::detail::CoalescedEventQueue<Key, Event, lholo::projection::detail::SubChunkKeyHash> pending;
        for (std::size_t i = 0; i < input; ++i) {
            Key const key{static_cast<int>(i % distinct), -64, 123};
            pending.push(key, Event{key, i + 1},
                [](auto& previous, auto const& latest) noexcept {
                    previous.second = (std::max)(previous.second, latest.second);
                });
        }
        auto batch = pending.take(4096);
        std::sort(batch.begin(), batch.end());
        std::uint64_t sum{};
        for (auto const& event : batch) sum += event.second;
        checksum = sum;
    }, 11);
    std::printf("world_events input=%zu distinct=%zu deque_enqueue_drain_us=%.3f coalesced_enqueue_drain_us=%.3f pending_before=%zu pending_after=%zu\n",
        input, distinct, baseline, compact, input, distinct);
    using Interest = lholo::projection::detail::WorldEventInterest;
    Interest const interest(std::vector<Interest::Box>{{{0, -64, 120}, {128, -63, 128}}});
    auto unrelated = [&](bool filter) {
        lholo::projection::detail::CoalescedEventQueue<Key, Event, lholo::projection::detail::SubChunkKeyHash> pending;
        for (std::size_t i = 0; i < input; ++i) {
            auto const x = static_cast<int>(i);
            if (filter && !interest.contains({x, -64, 123})) continue;
            Key const key{x, -64, 123};
            pending.push(key, Event{key, i + 1}, [](auto&, auto const&) noexcept {});
        }
        checksum = pending.size();
    };
    auto const beforeFilter = medianMicros([&] { unrelated(false); }, 11);
    auto const afterFilter = medianMicros([&] { unrelated(true); }, 11);
    std::printf("world_event_interest input=%zu distinct=%zu coalesced_unfiltered_us=%.3f filtered_us=%.3f pending_before=%zu pending_after=%zu\n",
        input, input, beforeFilter, afterFilter, input, distinct);
}

void sectionSelectionBench(std::size_t count) {
    // Match the 64-byte x64 SectionState stride without engine-owned objects.
    struct Section {
        std::array<float, 3> center;
        bool dirty, incrementalDirty, buildInFlight;
        std::uint64_t requestedRevision, uploadedRevision;
        std::array<std::uintptr_t, 4> meshSlots;
    };
    static_assert(sizeof(Section) == 64);
    std::vector<Section> original(count);
    for (std::size_t i = 0; i < count; ++i) {
        original[i].center = {static_cast<float>(i % 256), static_cast<float>(i / 256), 0};
        original[i].dirty = true;
    }
    auto select = [](std::vector<Section> const& sections) {
        return lholo::projection::detail::selectDirtySection(std::span<Section const>{sections},
            [](Section const& section) { auto const& c = section.center; return c[0]*c[0] + c[1]*c[1] + c[2]*c[2]; });
    };
    auto const oneAdmission = medianMicros([&] { checksum = select(original).value_or(count); });
    auto const convergence = medianMicros([&] {
        auto sections = original;
        std::uint64_t sum{};
        for (std::size_t i = 0; i < count; ++i) {
            auto const selected = select(sections);
            if (!selected) throw std::runtime_error("dirty section disappeared");
            sum += *selected;
            sections[*selected].dirty = false;
        }
        checksum = sum;
    }, 3);
    std::printf("dirty_selection sections=%zu stride=%zu one_admission_us=%.3f all_admissions_and_state_copy_us=%.3f\n",
        count, sizeof(Section), oneAdmission, convergence);
}

struct Position { float x{}, y{}, z{}; };
std::vector<Position> sectionSurface(float offsetX) {
    std::vector<Position> positions;
    positions.reserve(6 * 16 * 16 * 4);
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            for (int first = 0; first < 16; ++first) for (int second = 0; second < 16; ++second) {
                std::array<Position, 4> quad{};
                constexpr int corners[4][2]{{0,0}, {1,0}, {1,1}, {0,1}};
                for (std::size_t i = 0; i < quad.size(); ++i) {
                    std::array<float, 3> point{};
                    point[axis] = side == 0 ? 0.f : 16.f;
                    point[(axis + 1) % 3] = static_cast<float>(first + corners[i][0]);
                    point[(axis + 2) % 3] = static_cast<float>(second + corners[i][1]);
                    quad[i] = {point[0] + offsetX, point[1], point[2]};
                }
                if (side == 0) std::reverse(quad.begin(), quad.end());
                positions.insert(positions.end(), quad.begin(), quad.end());
            }
        }
    }
    return positions;
}

void liquidAggregationBench(std::size_t sectionCount) {
    using namespace lholo::projection::detail;
    std::vector<std::vector<Position>> sections;
    for (std::size_t i = 0; i < sectionCount; ++i) sections.push_back(sectionSurface(static_cast<float>(i * 16)));
    auto const totalVertices = sectionCount * sections.front().size();
    std::vector<LiquidSectionQuadCount> order;
    std::vector<Position> original;
    for (std::size_t i = 0; i < sectionCount; ++i) {
        order.push_back({i, sections[i].size() / 4});
        original.insert(original.end(), sections[i].begin(), sections[i].end());
    }
    auto const initialMask = buildNativeLiquidInternalFaceCullMask(std::span<Position const>{original});
    LiquidBoundaryMaskCache cache;
    if (!cache.remember(order, initialMask)) throw std::runtime_error("benchmark mask cache admission failed");
    auto aggregate = [&](bool reuseMask) {
        std::reverse(order.begin(), order.end());
        // Measure canonical position/kind concatenation, clone and the actual
        // cull and compaction rules. Native supplementary fields/GPU excluded.
        auto positions = sections[order.front().section];
        for (std::size_t i = 1; i < sectionCount; ++i) {
            auto const& source = sections[order[i].section];
            positions.insert(positions.end(), source.begin(), source.end());
        }
        std::vector<std::uint8_t> kinds(positions.size());
        auto candidate = positions;
        auto candidateKinds = kinds;
        auto const mask = reuseMask ? *cache.forOrder(order) : buildNativeLiquidInternalFaceCullMask(
            std::span<Position const>{candidate}, NativeLiquidFullFaceTolerance, std::span<std::uint8_t const>{candidateKinds});
        auto const expectedPairs = (sectionCount - 1) * 256;
        if (!mask.valid || mask.facePairs != expectedPairs) throw std::runtime_error("liquid boundary cull benchmark mismatch");
        compactLiquidQuadField(candidate, mask.removeQuads, 4);
        compactLiquidQuadField(candidateKinds, mask.removeQuads, 4);
        if (candidate.size() != totalVertices - expectedPairs * 8) throw std::runtime_error("liquid compaction mismatch");
        checksum = candidate.size();
    };
    auto const before = medianMicros([&] { aggregate(false); }, 11);
    auto const after = medianMicros([&] { aggregate(true); }, 11);
    std::printf("liquid_aggregate sections=%zu vertices=%zu position_and_kind_bytes=%zu fresh_assemble_clone_cull_compact_us=%.3f cached_assemble_clone_cull_compact_us=%.3f mask_bytes=%zu\n",
        sectionCount, totalVertices, totalVertices * (sizeof(Position) + sizeof(std::uint8_t)), before, after,
        initialMask.removeQuads.size());
}

void materialKeyBench(std::size_t blocks) {
    std::vector<std::string> names;
    for (std::size_t i = 0; i < 64; ++i) names.push_back("minecraft:material_variant_" + std::to_string(i));
    std::size_t factories{};
    auto const elapsed = medianMicros([&] {
        std::unordered_map<std::size_t, std::string> cache;
        factories = 0;
        std::uint64_t sum{};
        // assignMaterialIndices reads body/liquid identities in both its count
        // and assignment passes. Use 64 opaque palette identities here; native
        // Block::getTypeName, sorted material maps and file I/O are excluded.
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t i = 0; i < blocks; ++i) {
                for (auto const palette : {i % names.size(), (i * 7 + 3) % names.size()}) {
                    auto const& key = lholo::block::detail::cachedMaterialKey(cache, palette, [&] {
                        ++factories;
                        return lholo::block::materialKey(names[palette]);
                    });
                    sum += key.size();
                }
            }
        }
        checksum = sum;
    }, 7);
    std::printf("material_key blocks=%zu palette=64 lookups=%zu us=%.3f factories=%zu checksum=%llu\n",
        blocks, blocks * 4, elapsed, factories, static_cast<unsigned long long>(checksum));
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--material-keys") {
        for (auto const million : {1, 4}) materialKeyBench(static_cast<std::size_t>(million) * 1024 * 1024);
        return checksum == 0;
    }
    if (argc != 1) return 2;
    snapshotBench(1024 * 1024);
    snapshotBench(16 * 1024 * 1024);
    for (auto const million : {1, 4, 8, 16}) snapshotBench(static_cast<std::size_t>(million) * 1024 * 1024, true);
    nbtBench();
    eventBench();
    for (auto const sections : {1024, 4096, 16384}) sectionSelectionBench(sections);
    for (auto const sections : {8, 64}) liquidAggregationBench(sections);
    for (auto const million : {1, 4}) materialKeyBench(static_cast<std::size_t>(million) * 1024 * 1024);
    return checksum == 0;
}
