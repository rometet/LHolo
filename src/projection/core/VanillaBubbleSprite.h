// LHolo - R12 vanilla particle sprite geometry for ghost water columns.
// The UV rectangle and size formula derive from the 1.26.51.01 vanilla
// bubble_column_{up,down} compiled resource definitions. Does not spawn
// or alter real-world particles; the world-water expiry component is not used.
#pragma once
#include "projection/core/AnimatedBubbleGeometry.h"

namespace lholo::projection::detail {
inline constexpr float VanillaBubbleParticleAtlasWidth = 128.F;
inline constexpr float VanillaBubbleParticleAtlasHeight = 128.F;
inline constexpr float VanillaBubbleU0 = 0.F/VanillaBubbleParticleAtlasWidth;
inline constexpr float VanillaBubbleV0 = 16.F/VanillaBubbleParticleAtlasHeight;
inline constexpr float VanillaBubbleU1 = 8.F/VanillaBubbleParticleAtlasWidth;
inline constexpr float VanillaBubbleV1 = 24.F/VanillaBubbleParticleAtlasHeight;
struct VanillaBubbleSprite {
    std::array<AnimatedBubbleVec3,4> vertices{};
    float opacity{};
};

template<class Emit>
void emitVanillaBubbleSprites(
    AnimatedBubbleVec3 blockOrigin,
    AnimatedBubbleVec3 cameraLocal,
    bool dragDown,
    double clockSeconds,
    std::uint32_t stableSeed,
    Emit&& emit
) {
    if (!std::isfinite(clockSeconds)
        || !std::isfinite(cameraLocal.x) || !std::isfinite(cameraLocal.y)
        || !std::isfinite(cameraLocal.z)) return;
    constexpr double Tau = 6.283185307179586476925286766559;
    for (std::size_t particle=0;particle<AnimatedBubblePerCell;++particle) {
        std::uint32_t salt=stableSeed ^
            (static_cast<std::uint32_t>(particle) * 0x9e3779b9U);
        float const shift=bubbleUnit(salt ^ 0x7103U);
        float const speed=0.56F + 0.18F*bubbleUnit(salt ^ 0x85c7U);
        double const t=std::max(0.0,clockSeconds)*static_cast<double>(speed)+shift;
        float const phase=static_cast<float>(t-std::floor(t));
        float const p=dragDown ? 1.F-phase : phase;
        auto const x=.24F+.48F*bubbleUnit(salt ^ 0xab41U);
        auto const z=.24F+.48F*bubbleUnit(salt ^ 0x3f53U);
        // Match vanilla's two random variables in its particle size.
        float const size=(.05F*bubbleUnit(salt ^ 0x9989U)+.1F)
            * (.9F*bubbleUnit(salt ^ 0xfa39U)+.2F);
        float const angle=static_cast<float>(
            clockSeconds*3.141592653589793 + shift*Tau
        );
        float swirlX=dragDown ? .045F*std::cos(angle) : .014F*std::sin(angle);
        float swirlZ=dragDown ? .045F*std::sin(angle) : .014F*std::cos(angle);
        auto const center=bubbleAdd(blockOrigin,{x+swirlX,.11F+.78F*p,z+swirlZ});
        float fade=std::min(std::clamp(phase/.12F,0.F,1.F),
                            std::clamp((1.F-phase)/.17F,0.F,1.F));
        if(fade<.002F) continue;
        auto const facing=bubbleNormalize(
            bubbleSubtract(cameraLocal,center),{0.F,0.F,1.F});
        auto const right=bubbleNormalize(
            bubbleCross({0.F,1.F,0.F},facing),{1.F,0.F,0.F});
        auto const up=bubbleNormalize(
            bubbleCross(facing,right),{0.F,1.F,0.F});
        // Sprite size in vanilla is width/height; half-size for each axis.
        float const r=size*.5F;
        auto const offset=[&](float ru,float rv) {
            return bubbleAdd(center,bubbleAdd(
                bubbleMultiply(right,ru*r),bubbleMultiply(up,rv*r)));
        };
        VanillaBubbleSprite s{};
        s.vertices={offset(-1.F,1.F),offset(1.F,1.F),
                    offset(1.F,-1.F),offset(-1.F,-1.F)};
        s.opacity=fade;
        emit(s);
    }
}
} // namespace lholo::projection::detail
