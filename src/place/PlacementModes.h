// Value snapshot shared by the placement owner and the menu control plane.
#pragma once

#include <cstdint>

namespace lholo::place {

struct PlacementModes {
    bool enabled{};
    bool manual{};
    bool range{};
    std::uint64_t revision{};
};

} // namespace lholo::place
