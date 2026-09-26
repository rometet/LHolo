#include "projection/core/ProjectionLiquidUv.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>

using namespace lholo::projection::detail;

struct TestUv {
    float x{};
    float y{};
    constexpr bool operator==(TestUv const&) const = default;
};

int main() {
    int checks = 0;
    int failures = 0;
    auto expect = [&](bool condition, char const* name) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << name << "\n";
        }
    };

    std::array<TestUv, 4> native{{        {0.562500000f, 0.343750000f},
        {0.562500000f, 0.375000000f},
        {0.578125000f, 0.375000000f},
        {0.578125000f, 0.343750000f},
    }};
    auto preserved = native;
    auto preservedSpan = std::span<TestUv>{preserved.data(), preserved.size()};
    expect(validateNativeLiquidUv(preservedSpan), "valid native UV accepted");
    expect(preserved == native, "validation preserves native UV bytes");

    NativeLiquidAtlasRect rect{
        0.406250000f, 0.781250000f, 0.421875000f, 0.812500000f
    };
    auto remapped = native;
    auto remappedSpan = std::span<TestUv>{remapped.data(), remapped.size()};
    expect(remapNativeLiquidUvToAtlas(remappedSpan, rect), "control remap succeeds");
    expect(remapped != native, "manual remap changes 26.51 native UV");
    expect(std::fabs(remapped[0].x - rect.u0) < 1e-7f, "remap u0");
    expect(std::fabs(remapped[0].y - rect.v0) < 1e-7f, "remap v0");

    std::array<TestUv, 3> odd{{{0,0},{1,0},{1,1}}};
    expect(!validateNativeLiquidUv(std::span<TestUv>{odd.data(), odd.size()}),
           "non-quad UV count rejected");
    auto nanUv = native;
    nanUv[1].x = std::numeric_limits<float>::quiet_NaN();
    expect(!validateNativeLiquidUv(std::span<TestUv>{nanUv.data(), nanUv.size()}),
           "NaN rejected");

    auto infUv = native;
    infUv[2].y = std::numeric_limits<float>::infinity();
    expect(!validateNativeLiquidUv(std::span<TestUv>{infUv.data(), infUv.size()}),
           "Inf rejected");

    std::span<TestUv> empty{};
    expect(!validateNativeLiquidUv(empty), "empty UV rejected");
    expect(isValidNativeLiquidAtlasRect(rect), "control atlas rect valid");

    std::cout << "Candidate D1 native UV checks: "
              << checks << ", failures: " << failures << "\n";
    return failures == 0 ? 0 : 1;
}
