// LHolo - Pure neighbor-based shell surface rules.
#pragma once

namespace lholo::projection::detail {

// Only a fully opaque cube may act as a solid occluder. The callback returns
// false for absent, transparent or partial neighbors and stops at the first gap.
template <class OpaqueNeighbor> bool projectionSolidCellEnclosed(bool opaque, OpaqueNeighbor &&neighbor) {
    if (!opaque)
        return false;
    constexpr int offsets[6][3]{{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    for (auto const &offset : offsets) {
        if (!neighbor(offset[0], offset[1], offset[2]))
            return false;
    }
    return true;
}

// Bit order shared by shell faces and their incident outline edges.
template <class NeighborPriority>
unsigned char projectionShellFaces(int priority, NeighborPriority &&neighbor) {
    constexpr int offsets[6][3]{{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    unsigned char mask{};
    for (int face = 0; face < 6; ++face) {
        if (priority > neighbor(offsets[face][0], offsets[face][1], offsets[face][2])) {
            mask |= static_cast<unsigned char>(1u << face);
        }
    }
    return mask;
}

} // namespace lholo::projection::detail
