#pragma once
// R16: bounded in-process Easy Place counters. No hooks, heap allocations,
// disk writes or logging in the placement hot path.
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lholo::place::telemetry {

enum class Event : std::size_t {
    Tick, Auto, Range, Manual, NoTarget, NoMaterial, PlanRejected,
    FailedPlanCached, RecentCellLock, GlobalInterval, ManualRepeatGate,
    SwapRequested, PacketSent, NativeQueued, DispatchRejected, Suppressed,
    ConnectivityAccepted, ConnectivityRejected,
    StairQueued, StairPlanRejected, StairRotationDrift,
    InventorySwapBlocked, InventorySwapResolved,
    Count
};

enum class Stage : std::size_t { Tick, Ray, Plan, Count };

constexpr std::size_t eventCount=static_cast<std::size_t>(Event::Count);
constexpr std::size_t stageCount=static_cast<std::size_t>(Stage::Count);
constexpr std::size_t bucketCount=6;

struct Snapshot {
    std::array<std::uint64_t,eventCount> event{};
    std::array<std::uint64_t,stageCount> samples{};
    std::array<std::uint64_t,stageCount> totalUs{};
    std::array<std::uint64_t,stageCount> maxUs{};
    std::array<std::array<std::uint64_t,bucketCount>,stageCount> buckets{};
    [[nodiscard]] std::uint64_t count(Event e) const noexcept {
        return event[static_cast<std::size_t>(e)];
    }
    [[nodiscard]] std::uint64_t averageUs(Stage stage) const noexcept {
        auto const n=static_cast<std::size_t>(stage);
        return samples[n]?totalUs[n]/samples[n]:0;
    }
    [[nodiscard]] std::uint64_t maximumUs(Stage stage) const noexcept {
        return maxUs[static_cast<std::size_t>(stage)];
    }
    [[nodiscard]] std::uint64_t above4ms(Stage stage) const noexcept {
        auto const& v=buckets[static_cast<std::size_t>(stage)];
        return v[4]+v[5];
    }
};

class Counters {
    std::array<std::atomic<std::uint64_t>,eventCount> mEvents{};
    std::array<std::atomic<std::uint64_t>,stageCount> mSamples{};
    std::array<std::atomic<std::uint64_t>,stageCount> mTotals{};
    std::array<std::atomic<std::uint64_t>,stageCount> mMax{};
    std::array<std::array<std::atomic<std::uint64_t>,bucketCount>,stageCount> mBuckets{};
    std::atomic<std::uint64_t> mNextReportAt{0};

public:
    void count(Event event) noexcept {
        mEvents[static_cast<std::size_t>(event)].fetch_add(1,std::memory_order_relaxed);
    }
    void observe(Stage stage,std::uint64_t us) noexcept {
        auto const s=static_cast<std::size_t>(stage);
        mSamples[s].fetch_add(1,std::memory_order_relaxed);
        mTotals[s].fetch_add(us,std::memory_order_relaxed);
        auto max=mMax[s].load(std::memory_order_relaxed);
        while(max<us && !mMax[s].compare_exchange_weak(
              max,us,std::memory_order_relaxed,std::memory_order_relaxed)) {}
        std::size_t bucket=0;
        while(bucket<bucketCount-1 && us>=kThresholds[bucket]) ++bucket;
        mBuckets[s][bucket].fetch_add(1,std::memory_order_relaxed);
    }
    [[nodiscard]] std::optional<Snapshot> snapshotIfDue(
        std::uint64_t now,bool active
    ) noexcept {
        if(!active) return std::nullopt;
        auto expected=mNextReportAt.load(std::memory_order_relaxed);
        if(expected==0) {
            auto const next=now+5'000;
            (void)mNextReportAt.compare_exchange_strong(
                expected,next,std::memory_order_relaxed,std::memory_order_relaxed);
            return std::nullopt;
        }
        if(now<expected || !mNextReportAt.compare_exchange_strong(
                expected,now+5'000,std::memory_order_relaxed,std::memory_order_relaxed))
            return std::nullopt;
        Snapshot result;
        for(std::size_t i=0;i<eventCount;++i)
            result.event[i]=mEvents[i].exchange(0,std::memory_order_relaxed);
        for(std::size_t i=0;i<stageCount;++i) {
            result.samples[i]=mSamples[i].exchange(0,std::memory_order_relaxed);
            result.totalUs[i]=mTotals[i].exchange(0,std::memory_order_relaxed);
            result.maxUs[i]=mMax[i].exchange(0,std::memory_order_relaxed);
            for(std::size_t b=0;b<bucketCount;++b)
                result.buckets[i][b]=mBuckets[i][b].exchange(0,std::memory_order_relaxed);
        }
        return result;
    }
private:
    static constexpr std::array<std::uint64_t,bucketCount-1> kThresholds =
        {500,1000,2000,4000,8000};
};

inline Counters& counters() noexcept {
    static Counters singleton;
    return singleton;
}

inline void count(Event event) noexcept { counters().count(event); }

class ScopedTimer {
    Stage mStage;
    std::chrono::steady_clock::time_point mBegin;
public:
    explicit ScopedTimer(Stage stage) noexcept:
        mStage(stage),mBegin(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() noexcept {
        auto const elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-mBegin).count();
        counters().observe(mStage,elapsed>0?static_cast<std::uint64_t>(elapsed):0);
    }
    ScopedTimer(ScopedTimer const&)=delete;
    ScopedTimer& operator=(ScopedTimer const&)=delete;
};

} // namespace lholo::place::telemetry
