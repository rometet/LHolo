#pragma once
#include "structure/VerificationSelection.h"
#include <array>
#include <limits>

namespace lholo::projection::detail {
struct VerifierHighlightContext {
    std::uint64_t worldEpoch{};
    int dimension{};
    std::uint64_t loadedGeneration{};
    structure::Cell origin;
    int rotation{}, mirror{};
    structure::LayerAxis layerAxis{structure::LayerAxis::Y};
    structure::LayerDisplayMode layerMode{structure::LayerDisplayMode::All};
    int layer{};
    bool visible{true}, countExtras{true};
};

inline std::optional<std::array<int,3>> verifierHighlightPosition(
    structure::schematic::SelectedMistake const& target, VerifierHighlightContext const& context
) noexcept {
    auto const& stamp=target.stamp;
    if(!stamp.valid() || !context.visible || stamp.worldEpoch!=context.worldEpoch
        || stamp.dimension!=context.dimension || stamp.loadedGeneration!=context.loadedGeneration
        || stamp.placementOrigin!=context.origin || stamp.placementRotation!=context.rotation
        || stamp.placementMirror!=context.mirror || stamp.layerAxis!=context.layerAxis
        || stamp.layerMode!=context.layerMode || stamp.layer!=context.layer
        || stamp.visible!=context.visible || stamp.countExtras!=context.countExtras) return {};
    auto const& world=target.mismatch.world;
    constexpr auto low=static_cast<std::int64_t>((std::numeric_limits<int>::min)())+1;
    constexpr auto high=static_cast<std::int64_t>((std::numeric_limits<int>::max)())-1;
    for(auto value:{world.x,world.y,world.z}) if(value<low || value>high)return {};
    return std::array{static_cast<int>(world.x),static_cast<int>(world.y),static_cast<int>(world.z)};
}
} // namespace lholo::projection::detail
