// Per-block native shape isolation for redstone diodes.
// A BlockTessellator instance is reused throughout a section, but the engine
// maintains mutable current-shape fields between tessellateInWorld calls.
// Repeaters and comparators must not inherit the prior block's AABB.
#pragma once

namespace lholo::projection::detail {

template <class NativeTessellator>
class ScopedDiodeCurrentShape {
public:
    ScopedDiodeCurrentShape(NativeTessellator& renderer, bool diode) noexcept
    : renderer_(renderer), diode_(diode) {
        reset();
    }

    ~ScopedDiodeCurrentShape() noexcept { reset(); }

    ScopedDiodeCurrentShape(ScopedDiodeCurrentShape const&) = delete;
    ScopedDiodeCurrentShape& operator=(ScopedDiodeCurrentShape const&) = delete;

private:
    void reset() noexcept {
        if (!diode_) return;
        renderer_.mCurrentShapeSet = false;
        renderer_.mCurrentShapeBlock = nullptr;
    }

    NativeTessellator& renderer_;
    bool diode_;
};

} // namespace lholo::projection::detail
