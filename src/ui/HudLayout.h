#pragma once
#include <algorithm>
#include <cmath>
namespace lholo::ui {
struct HudLayout {
    bool custom{};float x{},y{},scale{1};int anchor{};
    bool operator==(HudLayout const&)const=default;
};
inline bool validHudLayout(HudLayout const& v)noexcept {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::abs(v.x)<=16384&&std::abs(v.y)<=16384
        &&std::isfinite(v.scale)&&v.scale>=.5f&&v.scale<=3&&v.anchor>=0&&v.anchor<=8;
}
inline float hudScaleMultiplier(float base,HudLayout const& v)noexcept {
    return v.custom&&validHudLayout(v)&&std::isfinite(base)&&base>0?std::clamp(base*v.scale,.5f,5.f)/base:1.f;
}
}
