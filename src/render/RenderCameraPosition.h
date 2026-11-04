#pragma once
#include <cmath>
#include <optional>
#include "mc/client/renderer/BaseActorRenderContext.h"

namespace lholo::render {
struct NativeCameraPosition { float x{}, y{}, z{}; };

// Read the SDK-declared camera target: the old byte-40 layout changed in 26.60.
// The render callback owns the BaseActorRenderContext and its Impl lifetime.
inline std::optional<NativeCameraPosition> readRenderCameraPosition(
    BaseActorRenderContext const& context
) {
    auto const* impl = context.mImpl.get();
    if (!impl) return std::nullopt;
    auto const camera = impl->mCameraTargetPosition.get();
    if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z))
        return std::nullopt;
    return NativeCameraPosition{camera.x, camera.y, camera.z};
}
} // namespace lholo::render