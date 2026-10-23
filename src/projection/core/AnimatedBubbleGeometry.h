// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi
//
// R11 procedural bubble sprites. CPU-only, deterministic, no native particle
// emitters and no world writes. All positions are projection-local, within
// their source block cell. Used by the normal alpha render pass only.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <utility>

namespace lholo::projection::detail {

struct AnimatedBubbleVec3 {
    float x{}, y{}, z{};
};
struct AnimatedBubbleBillboardQuad {
    std::array<AnimatedBubbleVec3, 4> corners{};
    float opacity{};
};
inline constexpr std::size_t AnimatedBubblePerCell = 4U;
inline constexpr std::size_t AnimatedBubbleSegments = 8U;
inline constexpr std::size_t AnimatedBubbleQuadsPerCell =
    AnimatedBubblePerCell * AnimatedBubbleSegments;
inline constexpr std::size_t AnimatedBubbleVerticesPerCell =
    AnimatedBubbleQuadsPerCell * 4U;
inline constexpr std::size_t AnimatedBubbleMaxVisibleCells = 48U;
inline constexpr std::size_t AnimatedBubbleMaxCachedCells = 4096U;
inline constexpr std::size_t AnimatedBubbleMaxCellsScannedPerFrame = 8192U;
inline constexpr float AnimatedBubbleViewRadius = 56.F;
inline constexpr float BubbleDiagonal = 0.7071067811865476F;
inline constexpr std::array<float,9> BubbleRingCos{
    1.F,BubbleDiagonal,0.F,-BubbleDiagonal,-1.F,-BubbleDiagonal,0.F,BubbleDiagonal,1.F
};
inline constexpr std::array<float,9> BubbleRingSin{
    0.F,BubbleDiagonal,1.F,BubbleDiagonal,0.F,-BubbleDiagonal,-1.F,-BubbleDiagonal,0.F
};

[[nodiscard]] inline AnimatedBubbleVec3 bubbleAdd(AnimatedBubbleVec3 a, AnimatedBubbleVec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] inline AnimatedBubbleVec3 bubbleSubtract(AnimatedBubbleVec3 a, AnimatedBubbleVec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] inline AnimatedBubbleVec3 bubbleMultiply(AnimatedBubbleVec3 a, float scalar) noexcept {
    return {a.x * scalar, a.y * scalar, a.z * scalar};
}
[[nodiscard]] inline float bubbleDot(AnimatedBubbleVec3 a, AnimatedBubbleVec3 b) noexcept {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
[[nodiscard]] inline AnimatedBubbleVec3 bubbleCross(AnimatedBubbleVec3 a, AnimatedBubbleVec3 b) noexcept {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
[[nodiscard]] inline AnimatedBubbleVec3 bubbleNormalize(
    AnimatedBubbleVec3 a, AnimatedBubbleVec3 fallback
) noexcept {
    float lenSquared = bubbleDot(a,a);
    if (!std::isfinite(lenSquared) || lenSquared < 0.000001F) return fallback;
    return bubbleMultiply(a,1.F/std::sqrt(lenSquared));
}
[[nodiscard]] inline std::uint32_t bubbleHash(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}
[[nodiscard]] inline float bubbleUnit(std::uint32_t n) noexcept {
    return static_cast<float>(bubbleHash(n) & 0xffffU) / 65535.F;
}

template <class Emit>
void emitAnimatedBubbleCell(
    AnimatedBubbleVec3 blockOrigin,
    AnimatedBubbleVec3 cameraLocal,
    bool dragDown,
    double elapsedSeconds,
    std::uint32_t stableSeed,
    Emit&& emit
) {
    if (!std::isfinite(elapsedSeconds)) return;
    if (!std::isfinite(blockOrigin.x) || !std::isfinite(blockOrigin.y)
        || !std::isfinite(blockOrigin.z) || !std::isfinite(cameraLocal.x)
        || !std::isfinite(cameraLocal.y) || !std::isfinite(cameraLocal.z)) {
        return;
    }
    // Keep phase periodic in double precision so even long-running worlds
    // cannot suffer a visible jump at arbitrary 16-second boundaries.
    double const clock = std::max(0.0, elapsedSeconds);
    constexpr float TwoPi = 6.2831853071795864769F;
    for (std::size_t particle=0;particle<AnimatedBubblePerCell;++particle) {
        auto const salt = stableSeed
            ^ (static_cast<std::uint32_t>(particle) * 0x9e3779b9U);
        float const shift = bubbleUnit(salt ^ 0x7103U);
        float const speed = 0.56F + 0.18F * bubbleUnit(salt ^ 0x85c7U);
        double const phaseUnwrapped = clock * static_cast<double>(speed)
            + static_cast<double>(shift);
        float const phase = static_cast<float>(
            phaseUnwrapped - std::floor(phaseUnwrapped)
        );
        float const progress = dragDown ? 1.F - phase : phase;
        float const spreadX = 0.24F + .48F * bubbleUnit(salt ^ 0xab41U);
        float const spreadZ = 0.24F + .48F * bubbleUnit(salt ^ 0x3f53U);
        float const radius = .038F + .030F * bubbleUnit(salt ^ 0x1167U);
        auto const position = bubbleAdd(blockOrigin,{
            spreadX + .020F * static_cast<float>(
                std::sin(clock * 2.4 + static_cast<double>(shift * TwoPi))
            ),
            .11F + .78F * progress,
            spreadZ + .020F * static_cast<float>(
                std::cos(clock * 1.8 + static_cast<double>(shift * TwoPi))
            )
        });
        // Fade out before each cyclic teleport: no abrupt pop at cell border.
        auto const fadeIn = std::clamp(phase / .12F,0.F,1.F);
        auto const fadeOut = std::clamp((1.F-phase) / .17F,0.F,1.F);
        float const fade = std::min(fadeIn,fadeOut);
        if (fade < .002F) continue;
        auto const towardsCamera = bubbleNormalize(
            bubbleSubtract(cameraLocal, position),{0.F,0.F,1.F}
        );
        auto const right = bubbleNormalize(
            bubbleCross({0.F,1.F,0.F},towardsCamera),{1.F,0.F,0.F}
        );
        auto const up = bubbleNormalize(
            bubbleCross(towardsCamera,right),{0.F,1.F,0.F}
        );
        // Cached ring angles avoid thousands of trig calls per render frame.
        auto vertex = [&](float r,std::size_t seg) {
            auto const off = bubbleAdd(
                bubbleMultiply(right,r*BubbleRingCos[seg]),
                bubbleMultiply(up,r*BubbleRingSin[seg])
            );
            return bubbleAdd(position,off);
        };
        for (std::size_t seg=0;seg<AnimatedBubbleSegments;++seg) {
            float const inner = radius * 0.73F;
            AnimatedBubbleBillboardQuad q{};
            q.corners = {vertex(radius,seg), vertex(radius,seg+1U),
                         vertex(inner,seg+1U), vertex(inner,seg)};
            // A faint highlight on one arc adds volume without using
            // a fixed white concrete disk that would obscure the water.
            q.opacity = fade * (seg >= 1U && seg <= 3U ? .68F : .43F);
            emit(q);
        }
    }
}
} // namespace lholo::projection::detail
