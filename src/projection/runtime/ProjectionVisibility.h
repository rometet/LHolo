#pragma once
class BaseActorRenderContext;
class Vec3;
namespace lholo::projection::detail {
struct ProjectionState;
void updateProjectionVisibility(ProjectionState& state, BaseActorRenderContext& context, Vec3 const& camera);
}
