// LHolo - Scoped access to the engine's shared shader color.
#pragma once

#include <cstdint>
#include "mc/deps/renderer/ShaderColor.h"

namespace lholo::render {

class ScopedShaderColor {
public:
    explicit ScopedShaderColor(ShaderColor& target, mce::Color color = {1.f, 1.f, 1.f, 1.f})
    : mTarget(target), mSaved(target.color.get()) {
        mTarget.color = color;
        mTarget.dirty = true;
    }

    ScopedShaderColor(ShaderColor& target, std::uint32_t abgr)
    : ScopedShaderColor(target, mce::Color{
        static_cast<float>(abgr & 0xFFU) / 255.f,
        static_cast<float>((abgr >> 8) & 0xFFU) / 255.f,
        static_cast<float>((abgr >> 16) & 0xFFU) / 255.f,
        static_cast<float>((abgr >> 24) & 0xFFU) / 255.f}) {}

    ~ScopedShaderColor() {
        mTarget.color = mSaved;
        // CPU restoration also needs a GPU constant update on the next draw.
        mTarget.dirty = true;
    }

    ScopedShaderColor(ScopedShaderColor const&) = delete;
    ScopedShaderColor& operator=(ScopedShaderColor const&) = delete;

private:
    ShaderColor& mTarget;
    mce::Color mSaved;
};

} // namespace lholo::render
