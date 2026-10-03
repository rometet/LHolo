#pragma once

#include <algorithm>
#include <cmath>

namespace lholo::projection::detail {

enum class ProjectedPistonPass { Native, Ghost, Skip };

inline float normalizedPistonOpacity(float opacity) noexcept {
    return std::isfinite(opacity) ? std::clamp(opacity, 0.0f, 1.0f) : 1.0f;
}

inline ProjectedPistonPass projectedPistonPass(float opacity, bool alphaPass) noexcept {
    auto const normalized = normalizedPistonOpacity(opacity);
    if (normalized == 0.0f) return ProjectedPistonPass::Skip;
    if (normalized >= 0.999f) return ProjectedPistonPass::Native;
    return alphaPass ? ProjectedPistonPass::Ghost : ProjectedPistonPass::Skip;
}

// Borrow an existing compatible material owner; never mutate its blend/depth
// state or rebuild the native model/UVs. Restore even if native submission throws.
// RGB is vanilla's lighting/tint; only its alpha is multiplied by the projection.
template <class MaterialOwner, class Color>
class ScopedPistonAppearance {
public:
    ScopedPistonAppearance(MaterialOwner& material, MaterialOwner const& blendMaterial,
                           Color& color, bool& dirty, float opacity)
        : mMaterial(material), mSavedMaterial(material), mColor(color),
          mSavedColor(color), mDirty(dirty) {
        mMaterial = blendMaterial;
        mColor.a *= normalizedPistonOpacity(opacity);
        mDirty = true;
    }
    ~ScopedPistonAppearance() {
        mMaterial = mSavedMaterial;
        mColor = mSavedColor;
        // Native rendering may have uploaded the temporary alpha. The next
        // owner must upload the restored value, even if it was clean on entry.
        mDirty = true;
    }
    ScopedPistonAppearance(ScopedPistonAppearance const&) = delete;
    ScopedPistonAppearance& operator=(ScopedPistonAppearance const&) = delete;

private:
    MaterialOwner& mMaterial;
    MaterialOwner mSavedMaterial;
    Color& mColor;
    Color mSavedColor;
    bool& mDirty;
};

} // namespace lholo::projection::detail
