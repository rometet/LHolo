#pragma once
#include <array>
#include <optional>

namespace lholo::structure::detail {

// Only explicit, valid saved world coordinates may be selected in the
// direct-source-origin mode. Never silently use the player's feet if absent.
inline bool validSavedWorldOrigin(std::array<int,3> const& p) noexcept {
    return p[0]>=-30000000 && p[0]<=30000000
        && p[2]>=-30000000 && p[2]<=30000000
        && p[1]>=-1000000 && p[1]<=1000000;
}
inline std::optional<std::array<int,3>> selectInitialLoadAnchor(
    bool useSavedWorldOrigin,
    std::optional<std::array<int,3>> const& clickedAnchor,
    std::optional<std::array<int,3>> const& sourceOrigin
) noexcept {
    if(!useSavedWorldOrigin)return clickedAnchor;
    if(!sourceOrigin || !validSavedWorldOrigin(*sourceOrigin))return std::nullopt;
    return sourceOrigin;
}

} // namespace lholo::structure::detail
