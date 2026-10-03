#pragma once

class PistonArmModel;
namespace mce { class MaterialPtr; }

namespace lholo::projection::detail {

struct ProjectedPistonRender {
    PistonArmModel const* model{};
    mce::MaterialPtr const* blendMaterial{};
    float opacity{1.0f};
};

// Stack-borrowed context, armed only around LHolo's projected dispatcher call.
// Real-world pistons, another thread, or a different model cannot inherit it.
inline thread_local ProjectedPistonRender const* activeProjectedPistonRender{};

class ScopedProjectedPistonRender {
public:
    explicit ScopedProjectedPistonRender(ProjectedPistonRender const* render) noexcept
        : mSaved(activeProjectedPistonRender) { activeProjectedPistonRender = render; }
    ~ScopedProjectedPistonRender() { activeProjectedPistonRender = mSaved; }
    ScopedProjectedPistonRender(ScopedProjectedPistonRender const&) = delete;
    ScopedProjectedPistonRender& operator=(ScopedProjectedPistonRender const&) = delete;
private:
    ProjectedPistonRender const* mSaved;
};

} // namespace lholo::projection::detail
