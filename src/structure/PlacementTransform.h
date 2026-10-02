#pragma once

#include "structure/LayerDisplayTypes.h"
#include <algorithm>
#include <cstdint>

namespace lholo::structure {
// Canonical cell transform. Origin is the minimum corner of the turned box;
// mirror flips the named coordinate BEFORE rotation. Wide coordinates also
// support neighbor queries outside the box without signed overflow.
struct Cell {
    std::int64_t x{}, y{}, z{};
    bool operator==(Cell const&) const = default;
};
struct PlacementTransform {
    Cell size;
    Cell origin;
    int rotation{};
    int mirror{}; // stable LHolo encoding: 0 none, 1 flip X, 2 flip Z

    Cell placedSize() const { return rotation & 1 ? Cell{size.z, size.y, size.x} : size; }
    Cell toWorld(Cell local) const {
        if (mirror == 1) local.x = size.x - 1 - local.x;
        if (mirror == 2) local.z = size.z - 1 - local.z;
        Cell turned;
        switch (rotation & 3) {
        case 1: turned = {size.z - 1 - local.z, local.y, local.x}; break;
        case 2: turned = {size.x - 1 - local.x, local.y, size.z - 1 - local.z}; break;
        case 3: turned = {local.z, local.y, size.x - 1 - local.x}; break;
        default: turned = local; break;
        }
        return {origin.x + turned.x, origin.y + turned.y, origin.z + turned.z};
    }
    Cell toLocal(Cell world) const {
        Cell p{world.x - origin.x, world.y - origin.y, world.z - origin.z};
        Cell local;
        switch (rotation & 3) {
        case 1: local = {p.z, p.y, size.z - 1 - p.x}; break;
        case 2: local = {size.x - 1 - p.x, p.y, size.z - 1 - p.z}; break;
        case 3: local = {size.x - 1 - p.z, p.y, p.x}; break;
        default: local = p; break;
        }
        if (mirror == 1) local.x = size.x - 1 - local.x;
        if (mirror == 2) local.z = size.z - 1 - local.z;
        return local;
    }
    bool contains(Cell world) const {
        auto const local = toLocal(world);
        return local.x >= 0 && local.y >= 0 && local.z >= 0
            && local.x < size.x && local.y < size.y && local.z < size.z;
    }
};

inline int layerCount(Cell placed, LayerAxis axis) {
    switch (axis) {
    case LayerAxis::X: case LayerAxis::WestToEast: case LayerAxis::EastToWest: return static_cast<int>(placed.x);
    case LayerAxis::NorthToSouth: case LayerAxis::SouthToNorth: return static_cast<int>(placed.z);
    default: return static_cast<int>(placed.y);
    }
}
inline int layerOf(PlacementTransform const& transform, Cell local, LayerAxis axis) {
    // Persisted legacy X/Y group in SOURCE coordinates; six new directions
    // group in PLACED world axes. Do not reinterpret old settings.
    if (axis == LayerAxis::X) return static_cast<int>(local.x);
    if (axis == LayerAxis::Y || axis == LayerAxis::Material) return static_cast<int>(local.y);
    auto const p = transform.toWorld(local);
    auto const size = transform.placedSize();
    switch (axis) {
    case LayerAxis::TopToBottom: return static_cast<int>(size.y - 1 - (p.y - transform.origin.y));
    case LayerAxis::WestToEast: return static_cast<int>(p.x - transform.origin.x);
    case LayerAxis::EastToWest: return static_cast<int>(size.x - 1 - (p.x - transform.origin.x));
    case LayerAxis::NorthToSouth: return static_cast<int>(p.z - transform.origin.z);
    case LayerAxis::SouthToNorth: return static_cast<int>(size.z - 1 - (p.z - transform.origin.z));
    default: return static_cast<int>(p.y - transform.origin.y);
    }
}
inline char const* layerAxisLabel(LayerAxis axis) {
    switch (axis) {
    case LayerAxis::X: return "X (legacy)";
    case LayerAxis::Y: return "Y (legacy)";
    case LayerAxis::Material: return "Material";
    case LayerAxis::BottomToTop: return "Bottom -> Top";
    case LayerAxis::TopToBottom: return "Top -> Bottom";
    case LayerAxis::WestToEast: return "West -> East";
    case LayerAxis::EastToWest: return "East -> West";
    case LayerAxis::NorthToSouth: return "North -> South";
    case LayerAxis::SouthToNorth: return "South -> North";
    }
    return "Y";
}
} // namespace lholo::structure
