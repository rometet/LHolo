// LHolo - Comparison overlay preferences and CPU outline geometry.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace lholo::projection::detail {

inline constexpr float DefaultComparisonStrength = 1.0f;
inline constexpr float DefaultCorrectionOutlineWidth = 1.0f;

inline float normalizeComparisonStrength(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 2.0f) : DefaultComparisonStrength;
}

inline float normalizeCorrectionOutlineWidth(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 1.0f, 8.0f) : DefaultCorrectionOutlineWidth;
}

inline float scaledComparisonAlpha(float opacity, float strength) noexcept {
    if (!std::isfinite(opacity)) opacity = 0.0f;
    return std::clamp(std::clamp(opacity, 0.0f, 1.0f) * normalizeComparisonStrength(strength), 0.0f, 1.0f);
}

struct ComparisonStyle {
    float fillOpacity{};
    float outlineOpacity{};
    float strength{DefaultComparisonStrength};
    float outlineWidth{DefaultCorrectionOutlineWidth};
};

inline bool comparisonStyleChanged(ComparisonStyle const& before, ComparisonStyle const& after) noexcept {
    return std::abs(before.fillOpacity - after.fillOpacity) > 0.0001f
        || std::abs(before.outlineOpacity - after.outlineOpacity) > 0.0001f
        || std::abs(before.strength - after.strength) > 0.0001f
        || std::abs(before.outlineWidth - after.outlineWidth) > 0.0001f;
}

// Client preferences belong outside the world/dimension-owned ProjectionState.
class ComparisonPreferences {
public:
    float strength() const noexcept { return mStrength.load(std::memory_order_relaxed); }
    float outlineWidth() const noexcept { return mOutlineWidth.load(std::memory_order_relaxed); }
    void setStrength(float value) noexcept { mStrength.store(normalizeComparisonStrength(value), std::memory_order_relaxed); }
    void setOutlineWidth(float value) noexcept { mOutlineWidth.store(normalizeCorrectionOutlineWidth(value), std::memory_order_relaxed); }
private:
    std::atomic<float> mStrength{DefaultComparisonStrength};
    std::atomic<float> mOutlineWidth{DefaultCorrectionOutlineWidth};
};

using ComparisonPoint = std::array<float, 3>;
using ComparisonQuad = std::array<ComparisonPoint, 4>;

inline bool usesThickCorrectionOutline(float width) noexcept {
    return normalizeCorrectionOutlineWidth(width) > DefaultCorrectionOutlineWidth;
}

// Width 1 uses the original screen-width LineList. Wider values use actual
// block-space prisms, 0.5% of a block per width unit (maximum 4%). No camera,
// world lighting or shared material lifetime is retained by the CPU builder.
template <class Emit>
void emitThickComparisonEdge(ComparisonPoint first, ComparisonPoint second, float width, Emit&& emit) {
    if (!usesThickCorrectionOutline(width)) return;
    auto const half = normalizeCorrectionOutlineWidth(width) * 0.0025f;
    ComparisonPoint lo{}, hi{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(first[axis]) || !std::isfinite(second[axis])) return;
        lo[axis] = std::min(first[axis], second[axis]) - half;
        hi[axis] = std::max(first[axis], second[axis]) + half;
    }
    auto const [x0,y0,z0] = lo;
    auto const [x1,y1,z1] = hi;
    emit(ComparisonQuad{{{x0,y0,z0},{x0,y1,z0},{x1,y1,z0},{x1,y0,z0}}});
    emit(ComparisonQuad{{{x1,y0,z1},{x1,y1,z1},{x0,y1,z1},{x0,y0,z1}}});
    emit(ComparisonQuad{{{x0,y0,z1},{x0,y1,z1},{x0,y1,z0},{x0,y0,z0}}});
    emit(ComparisonQuad{{{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}}});
    emit(ComparisonQuad{{{x0,y0,z1},{x0,y0,z0},{x1,y0,z0},{x1,y0,z1}}});
    emit(ComparisonQuad{{{x0,y1,z0},{x0,y1,z1},{x1,y1,z1},{x1,y1,z0}}});
}

} // namespace lholo::projection::detail
