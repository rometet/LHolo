#pragma once
#include <cmath>
#include <cstdint>
namespace lholo::place::detail {
// Uses genuine, strictly increasing native input ticks. Never creates packets.
struct ExternalRotationWarmup {
    std::uint64_t lastTick{};
    unsigned primes{};
    bool observed{};
    bool fresh(std::uint64_t tick) const noexcept { return !observed || tick > lastTick; }
    bool ready(std::uint64_t tick) const noexcept { return primes >= 2 && fresh(tick); }
    void forwardedPrime(std::uint64_t tick) noexcept {
        if (!fresh(tick)) return;
        observed=true;lastTick=tick;if (primes<2) ++primes;
    }
    void reset() noexcept { *this = {}; }
};
struct ExternalMoveVector { float x{}, y{}; };
inline ExternalMoveVector preserveWorldMove(float x,float y,float originalYaw,float transmittedYaw) noexcept {
    auto const angle=(originalYaw-transmittedYaw)*0.01745329251994329577f;
    auto const c=std::cos(angle),s=std::sin(angle);
    return {c*x-s*y,s*x+c*y};
}
}
