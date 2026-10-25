#include "render/BoxWireframeMesh.h"

#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"

namespace lholo::render {

std::unique_ptr<mce::Mesh> buildBoxWireframeMesh(
    Tessellator& tessellator, Vec3 const& minimum, Vec3 const& maximum,
    std::string_view debugName
) {
    auto const vertices = boxWireframeVertices(minimum, maximum);
    tessellator.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::LineList, 24, false);
    tessellator.color(1.f, 1.f, 1.f, 1.f);
    for (auto const& vertex : vertices) {
        tessellator.tex2({0.5f, 0.5f});
        tessellator.vertex(vertex.x, vertex.y, vertex.z);
    }
    return std::make_unique<mce::Mesh>(tessellator.end(
        Tessellator::UploadMode::Buffered, debugName, SupplementaryFieldAutoGenerationMode{0}));
}

} // namespace lholo::render
