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
// Native player facing and the schematic direction are not guaranteed to
// reach the server together on the first input packet. When they differ,
// prime genuine input ticks before the item-use transaction (Praxis pattern).
// A native packet already facing the requested direction needs no extra delay.
inline bool needsOrientationWarmup(bool external, bool requireSync,
    float packetPitch, float packetYaw, float requestedPitch, float requestedYaw) noexcept {
    if (!requireSync) return false;
    if (external) return true; // Preserve the tested Praxis / BDS contract.
    if (!std::isfinite(packetPitch) || !std::isfinite(packetYaw)) return true;
    return std::fabs(packetPitch-requestedPitch)>0.1F
        || std::fabs(std::remainder(packetYaw-requestedYaw,360.F))>0.1F;
}
struct ExternalMoveVector { float x{}, y{}; };
inline ExternalMoveVector preserveWorldMove(float x,float y,float originalYaw,float transmittedYaw) noexcept {
    auto const angle=(originalYaw-transmittedYaw)*0.01745329251994329577f;
    auto const c=std::cos(angle),s=std::sin(angle);
    return {c*x-s*y,s*x+c*y};
}
}
