#pragma once
#include "projection/core/ComparisonStyle.h"
#include <limits>
#include <vector>

namespace lholo::tests {
template <class Check>
void runComparisonStyleChecks(Check&& check) {
    using namespace projection::detail;
    auto const nan = std::numeric_limits<float>::quiet_NaN();
    auto const inf = std::numeric_limits<float>::infinity();
    for (auto value : {nan, inf, -inf}) {
        check(normalizeComparisonStrength(value) == 1.f);
        check(normalizeCorrectionOutlineWidth(value) == 1.f);
    }
    check(normalizeComparisonStrength(-1) == 0);
    check(normalizeComparisonStrength(3) == 2);
    check(normalizeCorrectionOutlineWidth(0) == 1);
    check(normalizeCorrectionOutlineWidth(9) == 8);
    for (float opacity : {0.f, .15f, .5f, 1.f}) {
        check(scaledComparisonAlpha(opacity, 1) == opacity);
        check(scaledComparisonAlpha(opacity, 0) == 0);
        check(scaledComparisonAlpha(opacity, 2) >= opacity);
        check(scaledComparisonAlpha(opacity, 2) <= 1);
    }
    check(scaledComparisonAlpha(nan, 1) == 0);
    ComparisonPreferences prefs;
    check(prefs.strength() == 1 && prefs.outlineWidth() == 1);
    prefs.setStrength(1.7f); prefs.setOutlineWidth(4);
    check(prefs.strength() == 1.7f && prefs.outlineWidth() == 4);
    prefs.setStrength(nan); prefs.setOutlineWidth(inf);
    check(prefs.strength() == 1 && prefs.outlineWidth() == 1);
    ComparisonStyle before{.15f, 1, 1, 1};
    auto after = before;
    check(!comparisonStyleChanged(before, after));
    after.strength = 1.3f; check(comparisonStyleChanged(before, after));
    after = before; after.outlineWidth = 5; check(comparisonStyleChanged(before, after));
    std::vector<ComparisonQuad> faces;
    auto emit = [&](ComparisonQuad const& quad) { faces.push_back(quad); };
    emitThickComparisonEdge({0,0,0}, {1,0,0}, 1, emit);
    check(faces.empty());
    emitThickComparisonEdge({nan,0,0}, {1,0,0}, 8, emit);
    check(faces.empty());
    // Every face must wind outward on each axis and both edge directions.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        float previousThickness{};
        for (float width : {1.1f, 2.f, 4.f, 8.f}) {
            ComparisonPoint first{-17,-64,27}, second = first;
            second[axis] += 1;
            faces.clear(); emitThickComparisonEdge(first, second, width, emit);
            check(faces.size() == 6);
            auto lo = faces.front().front(), hi = lo;
            ComparisonPoint center{};
            for (std::size_t i=0;i<3;++i) center[i]=(first[i]+second[i])*0.5f;
            for (auto const& face : faces) {
                ComparisonPoint a{}, b{}, faceCenter{};
                for (std::size_t i=0;i<3;++i) {
                    a[i]=face[1][i]-face[0][i]; b[i]=face[2][i]-face[0][i];
                    for (auto const& point : face) {
                        check(std::isfinite(point[i]));
                        lo[i]=std::min(lo[i],point[i]); hi[i]=std::max(hi[i],point[i]);
                        faceCenter[i]+=point[i]*0.25f;
                    }
                }
                ComparisonPoint normal{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
                float outward{};
                for(std::size_t i=0;i<3;++i) outward+=normal[i]*(faceCenter[i]-center[i]);
                check(outward > 0);
            }
            auto const crossAxis = (axis+1)%3;
            float const thickness=hi[crossAxis]-lo[crossAxis];
            check(std::abs(thickness-width*.005f) < .00002f);
            check(thickness > previousThickness);
            check(thickness < .041f);
            previousThickness=thickness;
            auto forward = faces;
            faces.clear(); emitThickComparisonEdge(second, first, width, emit);
            check(faces == forward);
        }
    }
}
} // namespace lholo::tests
