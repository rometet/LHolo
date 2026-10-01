#pragma once
#include <cstdint>

namespace lholo::projection::detail {

// Diagnostic scans/logs are O(section count). Coalesce changing/empty builds
// to one snapshot per second without affecting geometry submission or uploads.
class MeshDiagnosticGate {
public:
    bool inspect(bool pending, std::uint64_t now) noexcept {
        if (!pending || (mInspected && now - mLastInspection < 1000)) return false;
        mInspected = true;
        mLastInspection = now;
        return true;
    }
private:
    bool mInspected{};
    std::uint64_t mLastInspection{};
};

} // namespace lholo::projection::detail
