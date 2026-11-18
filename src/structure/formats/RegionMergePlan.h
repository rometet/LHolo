#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace lholo::structure::detail {

// If source AABBs do not overlap, overlapping-cell ownership is impossible.
// This lets a native multi-region projection concatenate active cell lists
// without allocating a large per-cell unordered_map. Adjacent faces are fine.
template<class Box>
bool disjointRegionBounds(std::vector<Box> const& regions) noexcept {
    for (auto const& a : regions) {
        if (a.sizeX <= 0 || a.sizeY <= 0 || a.sizeZ <= 0) return false;
    }
    for (std::size_t i = 0; i < regions.size(); ++i) {
        auto const& a = regions[i];
        for (std::size_t j = i + 1; j < regions.size(); ++j) {
            auto const& b = regions[j];
            auto const separated =
                static_cast<std::int64_t>(a.x) + a.sizeX <= b.x ||
                static_cast<std::int64_t>(b.x) + b.sizeX <= a.x ||
                static_cast<std::int64_t>(a.y) + a.sizeY <= b.y ||
                static_cast<std::int64_t>(b.y) + b.sizeY <= a.y ||
                static_cast<std::int64_t>(a.z) + a.sizeZ <= b.z ||
                static_cast<std::int64_t>(b.z) + b.sizeZ <= a.z;
            if (!separated) return false;
        }
    }
    return true;
}

// Compact one-byte ownership per source block; avoids retaining a second
// full vector<RenderBlock> for disjoint native composites. Invalid layouts
// fail closed, leaving the original full per-region vectors untouched.
template<class Box, class Cells>
std::vector<std::uint8_t> makeDisjointRegionOwners(
    std::vector<Box> const& regions, Cells const& cells) {
    if(regions.empty() || regions.size()>255 || cells.empty() ||
       !disjointRegionBounds(regions)) return {};
    std::vector<std::uint8_t> owners;
    owners.reserve(cells.size());
    for(auto const& cell:cells) {
        std::size_t owner=regions.size();
        for(std::size_t i=0;i<regions.size();++i){
            auto const& box=regions[i];
            if(cell.x>=box.x && cell.y>=box.y && cell.z>=box.z &&
               static_cast<std::int64_t>(cell.x)<static_cast<std::int64_t>(box.x)+box.sizeX &&
               static_cast<std::int64_t>(cell.y)<static_cast<std::int64_t>(box.y)+box.sizeY &&
               static_cast<std::int64_t>(cell.z)<static_cast<std::int64_t>(box.z)+box.sizeZ) {
                owner=i;break;
            }
        }
        if(owner==regions.size())return {};
        owners.push_back(static_cast<std::uint8_t>(owner));
    }
    return owners;
}

} // namespace lholo::structure::detail
