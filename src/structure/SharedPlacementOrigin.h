// LHolo: shared projections must be created at their server-recorded origin.
// This is a pure placement constructor: it must never read player coordinates.
#pragma once

#include "structure/PlacementSession.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lholo::structure::schematic {

[[nodiscard]] inline std::optional<SavedPlacement> makePlacementAt(
    std::uint64_t id, std::string const& file, Cell origin,
    int dimension, int rotation, int mirror
) {
    if (id == 0 || !safeSchematicPath(file)
        || rotation < 0 || rotation > 3 || mirror < 0 || mirror > 2
        || origin.x < -30000000 || origin.x > 30000000
        || origin.z < -30000000 || origin.z > 30000000
        || origin.y < -1000000 || origin.y > 1000000) return std::nullopt;
    SavedPlacement result;
    result.id = id;
    result.name = file.size() <= 128 ? file : "Schematic";
    result.file = file;
    result.dimension = dimension;
    result.origin = origin;
    result.rotation = rotation;
    result.mirror = mirror;
    return result;
}

} // namespace lholo::structure::schematic
