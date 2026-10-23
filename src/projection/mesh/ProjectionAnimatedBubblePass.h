// LHolo - R11 client-only animated bubble column schematic overlay.
// Called on the projection alpha render pass; never spawns world particles.
#pragma once
class BaseActorRenderContext;
class BlockPos;
class Vec3;
namespace mce { class MaterialPtr; }
namespace lholo::projection::detail {
struct ProjectionState;
void submitAnimatedBubbleOverlay(
    ProjectionState& state,
    BaseActorRenderContext& renderContext,
    BlockPos const& renderOrigin,
    Vec3 const& cameraWorld,
    float structureOpacity,
    mce::MaterialPtr const& projectedBlendMaterial
);
}
