// LHolo - targeted native interaction-angle search for Easy Place.
// Predict using vanilla Block::getPlacementBlock for each candidate; do not
// guess a Bedrock state/runtime ID from yaw/pitch or force a raw block write.
#pragma once
#include "place/PlacementDirectionRules.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace lholo::place::detail {

struct PlacementInteractionAngles {
    float pitch{}, yaw{};
};
enum class PlacementRotationSearch {
    None,
    Horizontal,
    FacingSixWay,
    Crafter3D
};
inline constexpr std::size_t MaxPlacementRotationProbes = 12U;

[[nodiscard]] constexpr PlacementRotationSearch placementRotationSearch(
    std::string_view name,
    bool lowerDoor = false
) noexcept {
    if (!name.starts_with("minecraft:")) return PlacementRotationSearch::None;
    if (name == "minecraft:crafter") return PlacementRotationSearch::Crafter3D;
    // Face-attached blocks (hopper, torch, buttons and lightning rods) are
    // already planned from their support face: interaction pitch is not a
    // substitute for the missing support. Do not override those routes.
    if (name == "minecraft:observer"
        || name == "minecraft:dispenser" || name == "minecraft:dropper"
        || name == "minecraft:piston" || name == "minecraft:sticky_piston"
        || name == "minecraft:barrel") {
        return PlacementRotationSearch::FacingSixWay;
    }
    if (isGlazedTerracotta(name) || lowerDoor || name == "minecraft:trapdoor"
        || name.ends_with("_trapdoor") || name.ends_with("_stairs")
        || name == "minecraft:unpowered_repeater"
        || name == "minecraft:powered_repeater"
        || name == "minecraft:unpowered_comparator"
        || name == "minecraft:powered_comparator"
        || name == "minecraft:tripwire_hook"
        || name == "minecraft:lectern"
        || name == "minecraft:calibrated_sculk_sensor"
        || name == "minecraft:chiseled_bookshelf"
        || name == "minecraft:fence_gate"
        || name.ends_with("_fence_gate")
        || name.ends_with("_shelf")
        || name == "minecraft:lever"
        || name == "minecraft:bell") {
        return PlacementRotationSearch::Horizontal;
    }
    return PlacementRotationSearch::None;
}

struct PlacementRotationProbePlan {
    std::array<PlacementInteractionAngles,MaxPlacementRotationProbes> candidates{};
    std::size_t size{};
};

// In-place RAII changes only the player logic mRot during the native predictor
// call, not camera/head/body orientation or game ticks. The send path later
// attaches precisely the matched interact angles to a fresh native input.
template<class Rotation>
class ScopedPlacementRotation {
    Rotation& rotation;
    Rotation original;
public:
    ScopedPlacementRotation(Rotation& value,float pitch,float yaw)
      : rotation(value),original(value) {
        rotation.x=pitch;
        rotation.y=yaw;
    }
    ~ScopedPlacementRotation() noexcept {rotation=original;}
    ScopedPlacementRotation(ScopedPlacementRotation const&)=delete;
    ScopedPlacementRotation& operator=(ScopedPlacementRotation const&)=delete;
};

[[nodiscard]] inline PlacementRotationProbePlan makePlacementRotationProbes(
    PlacementRotationSearch kind,float currentPitch,float currentYaw
) noexcept {
    PlacementRotationProbePlan result{};
    if (kind == PlacementRotationSearch::None
        || !std::isfinite(currentPitch) || !std::isfinite(currentYaw)) return result;
    auto const add = [&](float pitch,float yaw) {
        if (result.size >= result.candidates.size() || !std::isfinite(pitch)
            || !std::isfinite(yaw) || std::fabs(pitch) > 90.F) return;
        auto sameYaw = [](float a,float b) {
            // Equivalent wraparound yaw angles must not consume native calls.
            return std::fabs(std::remainder(a-b,360.F)) < .05F;
        };
        if (std::fabs(pitch-currentPitch)<.05F && sameYaw(yaw,currentYaw))
            return;
        for (std::size_t i=0;i<result.size;++i) {
            auto const& existing=result.candidates[i];
            if (std::fabs(existing.pitch-pitch)<.05F
                && sameYaw(existing.yaw,yaw)) return;
        }
        result.candidates[result.size++]={pitch,yaw};
    };
    constexpr std::array<float,4> yaws{0.F,90.F,180.F,-90.F};
    // Preserve click-height prediction for stairs/trapdoors/doors; only yaw
    // changes in these horizontal classes (same as previously tested route).
    float const horizontalPitch=kind==PlacementRotationSearch::Horizontal
        ? currentPitch : 0.F;
    for (auto const yaw:yaws) add(horizontalPitch,yaw);
    if (kind==PlacementRotationSearch::FacingSixWay) {
        // The face-dependent vanilla predictor must determine which pitch
        // actually produces up/down; these are just bounded search inputs.
        add(-80.F,currentYaw);
        add(80.F,currentYaw);
    } else if (kind==PlacementRotationSearch::Crafter3D) {
        // Crafter orientation may encode BOTH vertical axis and yaw, so all
        // twelve combinations need to be considered, max and no more.
        for (auto const pitch:{-80.F,80.F}) {
            for (auto const yaw:yaws) add(pitch,yaw);
        }
    }
    return result;
}

} // namespace lholo::place::detail
