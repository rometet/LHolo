#pragma once
namespace lholo::place::detail {
// The SDK Actor::getRotation accessor returns the mutable actor logic mRot.
// Do not call camera/head/body/interpolation setters during prediction.
template<class Rotation> class ScopedPlacementYaw {
    Rotation& rotation;
    Rotation saved;
public:
    ScopedPlacementYaw(Rotation& value,float yaw):rotation(value),saved(value) { rotation.y=yaw; }
    ~ScopedPlacementYaw() noexcept { rotation=saved; }
    ScopedPlacementYaw(ScopedPlacementYaw const&)=delete;
    ScopedPlacementYaw& operator=(ScopedPlacementYaw const&)=delete;
};
}
