#include "render/BoxWireframeMesh.h"
#include <bit>

#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"

namespace lholo::render {

std::unique_ptr<mce::Mesh> buildBoxWireframeMesh(
    Tessellator& tessellator, Vec3 const& minimum, Vec3 const& maximum,
    std::string_view debugName, std::uint16_t edgeMask
) {
    auto const vertices = boxWireframeVertices(minimum, maximum);
    edgeMask &= kAllBoxWireframeEdges;
    if(edgeMask==0)return {};
    tessellator.begin(Tessellator::DebugContextCallback{},
        mce::PrimitiveMode::LineList,
        static_cast<int>(2*std::popcount(static_cast<unsigned>(edgeMask))), false);
    tessellator.color(1.f, 1.f, 1.f, 1.f);
    for(std::size_t edge=0;edge<12;++edge) {
        if(!(edgeMask&(1u<<edge)))continue;
        for(std::size_t vertexIndex=2*edge;vertexIndex<2*edge+2;++vertexIndex) {
            auto const& vertex=vertices[vertexIndex];
            tessellator.tex2({0.5f, 0.5f});
            tessellator.vertex(vertex.x, vertex.y, vertex.z);
        }
    }
    return std::make_unique<mce::Mesh>(tessellator.end(
        Tessellator::UploadMode::Buffered, debugName, SupplementaryFieldAutoGenerationMode{0}));
}

} // namespace lholo::render
