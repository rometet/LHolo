#include "projection/core/ProjectionLiquidAppearance.h"
#include "projection/core/ProjectionLiquidCompatColor.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace lholo::projection::detail;
using uint = unsigned;
namespace {
unsigned checks{};
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (0)
std::uint32_t currentDerived(std::uint32_t raw, bool isWater, float opacity) {
    struct { float structureOpacity; } settings{opacity};
    auto const seed = selectPraxisCompatLiquidColorSeed(raw, isWater);
#include "CurrentDerivedAlpha.inc"
    return derived;
}
std::uint32_t baselineDerived(std::uint32_t raw, bool isWater) {
    auto const seed = selectPraxisCompatLiquidColorSeed(raw, isWater);
#include "BaselineDerivedAlpha.inc"
    return derived;
}
std::uint32_t currentProxy(float structureOpacity) {
#include "CurrentProxyAlpha.inc"
    return alpha;
}
std::uint32_t baselineProxy(float structureOpacity) {
#include "BaselineProxyAlpha.inc"
    return alpha;
}
void opacityChecks() {
    // Reproduce the baseline inconsistency before requiring the corrected
    // contract. At 100% every accepted derived RGB/alpha remains identical.
    CHECK(unpackAbgr(baselineDerived(0xffffffffU, true)).alpha == 160);
    CHECK(currentDerived(0xffffffffU, true, .5F) != baselineDerived(0xffffffffU, true));
    CHECK(baselineProxy(.01F) == 13 && currentProxy(.01F) == 3);
    for (bool water : {false, true}) {
        for (auto raw : std::array{0xffffffffU, 0xff808080U, 0xff303030U, 0xff102040U, 0x00000000U}) {
            auto const accepted = baselineDerived(raw, water);
            CHECK(currentDerived(raw, water, 1.F) == accepted);
            for (unsigned percent = 0; percent <= 100; ++percent) {
                auto const opacity = static_cast<float>(percent) / 100.F;
                auto const result = currentDerived(raw, water, opacity);
                CHECK((result & 0x00ffffffU) == (accepted & 0x00ffffffU));
                CHECK(unpackAbgr(result).alpha == static_cast<unsigned>(std::lround(
                    static_cast<float>(unpackAbgr(accepted).alpha) * opacity)));
            }
        }
    }
    CHECK(currentDerived(0xffffffffU, true, 0.F) >> 24U == 0);
    CHECK(currentDerived(0xffffffffU, true, .5F) >> 24U == 80);
    CHECK(currentDerived(0xffffffffU, false, .5F) >> 24U == 128);
    CHECK(currentProxy(0.F) == 0);
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        CHECK(currentDerived(0xffffffffU, true, invalid) == baselineDerived(0xffffffffU, true));
        CHECK(currentProxy(invalid) == 255);
    }
    CHECK(currentProxy(-1.F) == 0 && currentProxy(2.F) == 255);
}
#include "LightingSubmissionOrder.inc"
void lightingOrderChecks() {
    auto checkOrder=[](auto const& order) {
        bool primed{};
        unsigned unprimed{};
        for(auto name:order) {
            if(std::string_view{name}=="prime") primed=true;
            else if(!primed) ++unprimed;
        }
        return unprimed;
    };
    CHECK(checkOrder(baselineLightingOrder)==2);
    CHECK(checkOrder(currentLightingOrder)==0);
    // A liquid submission may replace constants. The original normal guard
    // still repairs them immediately before normal commands.
    bool primed{};
    for (auto name : currentLightingOrder) {
        auto token = std::string_view{name};
        if (token == "prime") primed = true;
        else if (token == "exact" || token == "retained") primed = false;
        else if (token == "normal") CHECK(primed);
    }
}
#include "RegionWriteScope.inc"
void projectedWriteChecks() {
    auto attempt=[] { return !regionWritesSuppressed(); };
    CHECK(attempt()); // baseline projected entries had no suppression
    {
        ScopedRegionWriteSuppression projected;
        CHECK(!attempt());
        bool workerSuppressed=true;
        std::thread worker([&] { workerSuppressed=regionWritesSuppressed(); });
        worker.join();
        CHECK(!workerSuppressed && !attempt());
        { ScopedRegionWriteSuppression nested; CHECK(!attempt()); }
        CHECK(!attempt());
    }
    CHECK(attempt());
    try {
        ScopedRegionWriteSuppression projected;
        CHECK(!attempt());
        throw std::runtime_error("projected renderer failure");
    } catch(std::runtime_error const&) {}
    CHECK(attempt()); // later real-world calls cannot inherit projection scope
}
}
#include "ProxyCommandChecks.h"
#include "BoundaryCommandChecks.h"
int main() {
    opacityChecks();
    proxyCommandChecks();
    lightingOrderChecks();
    projectedWriteChecks();
    boundaryCommandChecks();
    std::cout << "Render contract PASS " << checks << " checks; native rendering NOT_RUN\n";
}
