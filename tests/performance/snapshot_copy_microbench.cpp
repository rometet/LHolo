#include "projection/mesh/SectionBlockSnapshot.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <vector>

int main() {
    constexpr std::size_t cells = 2'000'000;
    constexpr int iterations = 64;
    std::vector<std::uint8_t> corrections(cells, 3), actors(cells, 1);
    std::vector<std::size_t> local;
    for (std::size_t start : {std::size_t{0}, std::size_t{300'000}, std::size_t{1'000'000}}) {
        for (std::size_t i = 0; i < 4096; ++i) local.push_back(start + i);
    }
    std::uint64_t checksum{};
    using Clock = std::chrono::steady_clock;
    auto const fullStart = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto const a = corrections;
        auto const b = actors;
        checksum += a[1'000'050] + b[300'050];
    }
    auto const fullEnd = Clock::now();
    lholo::projection::detail::SectionBlockSnapshot snapshot;
    auto const sparseStart = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        snapshot.capture(local, corrections, actors);
        checksum += snapshot.find(1'000'050)->correction + snapshot.find(300'050)->actorRenderer;
    }
    auto const sparseEnd = Clock::now();
    auto const fullMs = std::chrono::duration<double, std::milli>(fullEnd - fullStart).count();
    auto const sparseMs = std::chrono::duration<double, std::milli>(sparseEnd - sparseStart).count();
    std::printf("cells=%zu local=%zu iterations=%d full_ms=%.3f bounded_ms=%.3f full_bytes=%zu bounded_bytes=%zu checksum=%llu\n",
        cells, local.size(), iterations, fullMs, sparseMs, corrections.size()+actors.size(),
        snapshot.bytes(), static_cast<unsigned long long>(checksum));
    return snapshot.bytes() < corrections.size()+actors.size() ? 0 : 1;
}
