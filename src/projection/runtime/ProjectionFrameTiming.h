#pragma once
#include <atomic>
#include <cstdint>

namespace lholo::projection::detail {
struct ProjectionFrameTimings {
    std::uint64_t opaqueCount{}, alphaCount{};
    std::uint64_t opaqueTotalUs{}, alphaTotalUs{};
    std::uint64_t opaquePeakUs{}, alphaPeakUs{};
};
class ProjectionFrameTiming {
public:
    void record(bool alpha,std::uint64_t elapsedUs) noexcept {
        auto& count=alpha?mAlphaCount:mOpaqueCount;
        auto& total=alpha?mAlphaTotal:mOpaqueTotal;
        auto& peak=alpha?mAlphaPeak:mOpaquePeak;
        count.fetch_add(1,std::memory_order_relaxed);
        total.fetch_add(elapsedUs,std::memory_order_relaxed);
        auto old=peak.load(std::memory_order_relaxed);
        while(old<elapsedUs &&
            !peak.compare_exchange_weak(old,elapsedUs,std::memory_order_relaxed)) {}
    }
    ProjectionFrameTimings consume() noexcept {
        return {
            mOpaqueCount.exchange(0,std::memory_order_relaxed),
            mAlphaCount.exchange(0,std::memory_order_relaxed),
            mOpaqueTotal.exchange(0,std::memory_order_relaxed),
            mAlphaTotal.exchange(0,std::memory_order_relaxed),
            mOpaquePeak.exchange(0,std::memory_order_relaxed),
            mAlphaPeak.exchange(0,std::memory_order_relaxed)
        };
    }
private:
    std::atomic<std::uint64_t> mOpaqueCount{},mAlphaCount{};
    std::atomic<std::uint64_t> mOpaqueTotal{},mAlphaTotal{};
    std::atomic<std::uint64_t> mOpaquePeak{},mAlphaPeak{};
};
inline ProjectionFrameTiming& projectionFrameTiming() {
    static ProjectionFrameTiming timings;
    return timings;
}

// The CPU time spent issuing native correction draw calls is captured
// independently of the full projection callbacks. No GPU readbacks.
struct CorrectionSubmitStats {
    std::uint64_t fillCalls{}, outlineCalls{};
    std::uint64_t fillMicros{}, outlineMicros{};
    std::uint64_t fillPasses{}, outlinePasses{};
    std::uint64_t fillBatchedDraws{}, outlineBatchedDraws{};
};
class CorrectionSubmitTiming {
public:
    void record(bool outline, std::uint64_t elapsedUs, std::uint64_t draws,
                std::uint64_t batchedDraws = 0) noexcept {
        if (outline) {
            mOutlineCalls.fetch_add(draws, std::memory_order_relaxed);
            mOutlineMicros.fetch_add(elapsedUs, std::memory_order_relaxed);
            mOutlinePasses.fetch_add(1, std::memory_order_relaxed);
            mOutlineBatchedDraws.fetch_add(batchedDraws, std::memory_order_relaxed);
        } else {
            mFillCalls.fetch_add(draws, std::memory_order_relaxed);
            mFillMicros.fetch_add(elapsedUs, std::memory_order_relaxed);
            mFillPasses.fetch_add(1, std::memory_order_relaxed);
            mFillBatchedDraws.fetch_add(batchedDraws, std::memory_order_relaxed);
        }
    }
    CorrectionSubmitStats consume() noexcept {
        return {
            mFillCalls.exchange(0, std::memory_order_relaxed),
            mOutlineCalls.exchange(0, std::memory_order_relaxed),
            mFillMicros.exchange(0, std::memory_order_relaxed),
            mOutlineMicros.exchange(0, std::memory_order_relaxed),
            mFillPasses.exchange(0, std::memory_order_relaxed),
            mOutlinePasses.exchange(0, std::memory_order_relaxed),
            mFillBatchedDraws.exchange(0, std::memory_order_relaxed),
            mOutlineBatchedDraws.exchange(0, std::memory_order_relaxed)
        };
    }
private:
    std::atomic<std::uint64_t> mFillCalls{},mOutlineCalls{};
    std::atomic<std::uint64_t> mFillMicros{},mOutlineMicros{};
    std::atomic<std::uint64_t> mFillPasses{},mOutlinePasses{};
    std::atomic<std::uint64_t> mFillBatchedDraws{},mOutlineBatchedDraws{};
};
inline CorrectionSubmitTiming& correctionSubmitTiming() {
    static CorrectionSubmitTiming timings;
    return timings;
}

// Per-pass-only measurements: no clock reads or atomic writes per block actor.
// Candidate counts include offscreen and out-of-range actors; dispatch counts
// count attempted native render calls, not guaranteed GPU draws.
struct ProjectedActorPassStats {
    std::uint64_t calls{}, totalUs{}, candidateVisits{}, pistonDispatches{}, otherDispatches{};
};
class ProjectedActorPassTiming {
public:
    void record(std::uint64_t elapsedUs, std::uint64_t candidates,
                std::uint64_t pistonDispatches, std::uint64_t otherDispatches) noexcept {
        mCalls.fetch_add(1, std::memory_order_relaxed);
        mTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
        mCandidateVisits.fetch_add(candidates, std::memory_order_relaxed);
        mPistonDispatches.fetch_add(pistonDispatches, std::memory_order_relaxed);
        mOtherDispatches.fetch_add(otherDispatches, std::memory_order_relaxed);
    }
    ProjectedActorPassStats consume() noexcept {
        return {
            mCalls.exchange(0, std::memory_order_relaxed),
            mTotalUs.exchange(0, std::memory_order_relaxed),
            mCandidateVisits.exchange(0, std::memory_order_relaxed),
            mPistonDispatches.exchange(0, std::memory_order_relaxed),
            mOtherDispatches.exchange(0, std::memory_order_relaxed)
        };
    }
private:
    std::atomic<std::uint64_t> mCalls{}, mTotalUs{}, mCandidateVisits{};
    std::atomic<std::uint64_t> mPistonDispatches{}, mOtherDispatches{};
};
inline ProjectedActorPassTiming& projectedActorPassTiming() {
    static ProjectedActorPassTiming timings;
    return timings;
}
}
