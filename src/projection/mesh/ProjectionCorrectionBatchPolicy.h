#pragma once
#include <algorithm>
#include <cstddef>

namespace lholo::projection::detail {
// Conservative cross-frustum batch: the mesh contains the exact union of
// originals for 32 consecutive section IDs. Any visible member submits the
// batch, including geometry belonging to other (frustum-culled) members.
// GPU clipping rejects their out-of-frustum triangles; no visible member is
// ever dropped just because another member is outside the camera frustum.
constexpr std::size_t kCorrectionBatchSize = 32;
constexpr std::size_t kCorrectionBatchMinVisibleMeshes = 4;

template <class IsVisible>
bool correctionBatchHasVisibleMember(
    std::size_t first, std::size_t total, IsVisible&& isVisible
) {
    if (first >= total || first % kCorrectionBatchSize != 0) return false;
    auto const last = std::min(total, first + kCorrectionBatchSize);
    for (auto i = first; i < last; ++i)
        if (isVisible(i)) return true;
    return false;
}
} // namespace lholo::projection::detail
