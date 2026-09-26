#include "projection/core/ProjectionLiquidUvShadow.h"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <span>

using namespace lholo::projection::detail;

struct Uv { float x; float y; };

int main() {
    constexpr NativeLiquidAtlasRect current{0.10f, 0.20f, 0.20f, 0.30f};
    constexpr NativeLiquidAtlasRect old{0.50f, 0.60f, 0.70f, 0.80f};
    constexpr NativeLiquidAtlasRect near{0.10f + 0.00000005f, 0.20f, 0.20f, 0.30f};
    int checks = 0;
    auto check = [&](bool value) { assert(value); ++checks; };

    check(compareLiquidUvRects(current, current).exactEqual);
    check(compareLiquidUvRects(current, near).epsilonEqual);
    check(!compareLiquidUvRects(current, old).epsilonEqual);
    check(std::fabs(compareLiquidUvRects(current, old).maxAbsDelta - 0.5f) < 0.000001f);
    check(liquidUvTexturePathMatchesAlias("textures/blocks/water_still_grey.png", "water_still_grey"));
    check(!liquidUvTexturePathMatchesAlias("textures/blocks/missing.png", "water_still_grey"));
    check(!liquidUvTexturePathMatchesAlias("textures/blocks/water_still.png", "water_still_grey"));
    check(std::string_view{LiquidUvWaterStillAliases[0]} == "water_still_grey"
        && std::string_view{LiquidUvWaterStillAliases[3]} == "still_water");
    check(LiquidUvShadowMaxCapturedVertices == 240);

    BoundedLiquidUvRectSet rects;
    rects.add(current);
    rects.add(current);
    rects.add(old);
    check(rects.size == 2 && !rects.saturated);

    constexpr std::array<Uv, 4> raw{{{2, 3}, {4, 3}, {4, 5}, {2, 5}}};
    auto production = raw;
    auto shadow = raw;
    check(remapNativeLiquidUvToAtlas(std::span{production}, current));
    check(remapNativeLiquidUvToAtlas(std::span{shadow}, old));
    check(raw[0].x == 2 && raw[2].y == 5); // Diagnostic copies never mutate raw UV.
    check(production[0].x == current.u0 && production[0].y == current.v0);
    check(std::fabs(shadow[2].x - old.u1) < 0.000001f
        && std::fabs(shadow[2].y - old.v1) < 0.000001f);
    check(production[2].x != shadow[2].x);

    auto identicalShadow = raw;
    check(remapNativeLiquidUvToAtlas(std::span{identicalShadow}, current));
    check(identicalShadow[0].x == production[0].x
        && identicalShadow[2].y == production[2].y);

    std::cout << "liquid UV shadow: " << checks << " checks / 0 failures\n";
}
