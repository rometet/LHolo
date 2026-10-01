#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

#include <Windows.h>

namespace lholo::render {

struct NativeCameraPosition { float x{}, y{}, z{}; };

// Bedrock's opaque render-context Impl currently stores camera floats at
// byte offsets 40..51. Copy object bytes instead of inventing a float array
// object/alias. This checks mapped readability, not engine object lifetime;
// the render callback still owns that lifetime and the offset needs ABI QA.
inline std::optional<NativeCameraPosition> readRenderCameraPosition(void const* impl) {
    if (!impl) return std::nullopt;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(impl, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT
        || (memory.Protect & PAGE_GUARD)) return std::nullopt;
    switch (memory.Protect & 0xff) {
    case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY: break;
    default: return std::nullopt;
    }
    auto const begin = reinterpret_cast<std::uintptr_t>(impl);
    auto const regionBegin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    constexpr auto maximum = (std::numeric_limits<std::uintptr_t>::max)();
    if (begin > maximum - 52 || memory.RegionSize > maximum - regionBegin
        || begin + 52 > regionBegin + memory.RegionSize) return std::nullopt;
    std::array<float, 3> values;
    std::memcpy(values.data(), static_cast<unsigned char const*>(impl) + 40, sizeof(values));
    if (!std::isfinite(values[0]) || !std::isfinite(values[1]) || !std::isfinite(values[2])) return std::nullopt;
    return NativeCameraPosition{values[0], values[1], values[2]};
}

} // namespace lholo::render
