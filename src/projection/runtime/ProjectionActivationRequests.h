#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

namespace lholo::projection::detail {

struct ProjectionAnchor { int x{}, y{}, z{}; };
enum class DimensionActivationStatus : unsigned char { Ready, Deferred, Resuming };

// Anchors and suspension metadata are transactions. Separate coordinate
// atomics could combine two requests when a writer ran during consumption.
// This mutex never acquires the projection-state mutex or calls engine code.
class ProjectionActivationRequests {
public:
    std::optional<ProjectionAnchor> consumeAnchor() {
        std::lock_guard lock(mMutex);
        auto result = mPending;
        mPending.reset();
        return result;
    }
    void requestAnchor(int x, int y, int z) {
        std::lock_guard lock(mMutex);
        mPending = ProjectionAnchor{x, y, z};
    }
    void cancelAnchorRequest() {
        std::lock_guard lock(mMutex);
        mPending.reset();
    }
    void suspendForDimension(std::uint64_t generation, int dimension, ProjectionAnchor anchor) {
        std::lock_guard lock(mMutex);
        mSuspended = Suspension{generation, dimension, anchor};
    }
    DimensionActivationStatus prepareDimensionActivation(std::uint64_t generation, int dimension) {
        std::lock_guard lock(mMutex);
        if (!mSuspended) return DimensionActivationStatus::Ready;
        if (mSuspended->generation != generation) {
            mSuspended.reset();
            // A new restore request may have already published its own anchor.
            return DimensionActivationStatus::Ready;
        }
        if (mSuspended->dimension != dimension) return DimensionActivationStatus::Deferred;
        mPending = mSuspended->anchor;
        return DimensionActivationStatus::Resuming;
    }
    bool dimensionSuspended() const {
        std::lock_guard lock(mMutex);
        return mSuspended.has_value();
    }
    void cancelDimensionSuspension() {
        std::lock_guard lock(mMutex);
        mSuspended.reset();
    }
private:
    struct Suspension { std::uint64_t generation; int dimension; ProjectionAnchor anchor; };
    mutable std::mutex mMutex;
    std::optional<ProjectionAnchor> mPending;
    std::optional<Suspension> mSuspended;
};

} // namespace lholo::projection::detail
