// LHolo - Local-space box geometry; callers own transforms and mesh lifetime.
#pragma once

#include <array>
#include <memory>
#include <string_view>
#include "mc/deps/core/math/Vec3.h"

class Tessellator;
namespace mce { class Mesh; }

namespace lholo::render {

inline std::array<Vec3, 24> boxWireframeVertices(Vec3 const& minimum, Vec3 const& maximum) {
    auto const x0 = minimum.x, y0 = minimum.y, z0 = minimum.z;
    auto const x1 = maximum.x, y1 = maximum.y, z1 = maximum.z;
    return {{
        {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z0}, {x1,y1,z0},
        {x1,y1,z0}, {x0,y1,z0}, {x0,y1,z0}, {x0,y0,z0},
        {x0,y0,z1}, {x1,y0,z1}, {x1,y0,z1}, {x1,y1,z1},
        {x1,y1,z1}, {x0,y1,z1}, {x0,y1,z1}, {x0,y0,z1},
        {x0,y0,z0}, {x0,y0,z1}, {x1,y0,z0}, {x1,y0,z1},
        {x1,y1,z0}, {x1,y1,z1}, {x0,y1,z0}, {x0,y1,z1}
    }};
}

// Neutral vertex color; the selection material receives the box's color at
// submission via ScopedShaderColor. UVs always sample the white texture center.
// Called only on the render thread; section workers never build/upload this mesh.
std::unique_ptr<mce::Mesh> buildBoxWireframeMesh(
    Tessellator& tessellator, Vec3 const& minimum, Vec3 const& maximum,
    std::string_view debugName
);

} // namespace lholo::render
