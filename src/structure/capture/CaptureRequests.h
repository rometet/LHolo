#pragma once

#include "structure/capture/StructureCapture.h"
#include <utility>

namespace lholo::structure::capture::detail {
// Protected by the capture state mutex. Contains value data only; requests
// never carry a Minecraft pointer from Present to the game tick.
class CaptureRequests {
public:
    struct Export {
        Draft draft;
        std::filesystem::path output;
        std::uint64_t session;
    };
    void resetSession() { ++mSession; mPoints = 0; mExport.reset(); }
    std::uint64_t session() const noexcept { return mSession; }
    void requestPoint(PointSlot slot) noexcept {
        mPoints |= slot == PointSlot::First ? 1u : 2u;
    }
    unsigned takePoints() noexcept { return std::exchange(mPoints, 0); }
    void requestExport(Draft draft, std::filesystem::path output) {
        mExport.emplace(Export{std::move(draft), std::move(output), mSession});
    }
    std::optional<Export> takeExport() {
        auto request = std::move(mExport);
        mExport.reset();
        return request;
    }
private:
    std::uint64_t mSession{};
    unsigned mPoints{};
    std::optional<Export> mExport;
};
} // namespace lholo::structure::capture::detail
