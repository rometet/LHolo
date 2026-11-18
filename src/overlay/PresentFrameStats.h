#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lholo::overlay::detail {
struct PresentFrameReport {
    double fps{};
    double averageMs{};
    double p95Ms{};
    double p99Ms{};
    std::size_t frames{};
    bool projectionVisible{};
};
// Present-call cadence, not GPU execution time. Pure arithmetic; no graphics
// or world pointers retained across a swap-chain or resource-pack change.
class PresentFrameStats {
public:
    std::optional<PresentFrameReport> observe(std::int64_t nowUs, bool gameplay,
                                              bool projectionVisible=true) {
        if (!gameplay) {reset(); return std::nullopt;}
        if (!mActive || mProjectionVisible != projectionVisible) {
            reset();
            mActive=true; mProjectionVisible=projectionVisible;
            mLast=nowUs; mSince=nowUs;
            return std::nullopt;
        }
        auto const delta=nowUs-mLast;
        mLast=nowUs;
        if (delta<=0 || delta>2000000) {
            reset();
            mActive=true; mLast=nowUs; mSince=nowUs;
            return std::nullopt;
        }
        mSamples[mWritten % mSamples.size()]=delta;
        ++mWritten; ++mFrames;
        mTotalUs+=delta;
        if (nowUs-mSince<5000000 || mFrames<30) return std::nullopt;
        PresentFrameReport result;
        result.frames=mFrames;
        result.projectionVisible=mProjectionVisible;
        result.fps=1000000.0*static_cast<double>(mFrames)
            /static_cast<double>(nowUs-mSince);
        result.averageMs=static_cast<double>(mTotalUs)/mFrames/1000.0;
        auto const size=(std::min)(mWritten,mSamples.size());
        std::array<std::int64_t,1024> copy{};
        for(std::size_t i=0;i<size;++i) copy[i]=mSamples[i];
        auto const atPercentile=[&](std::size_t perThousand){
            auto const index=((size-1)*perThousand+999)/1000;
            std::nth_element(copy.begin(),copy.begin()+index,copy.begin()+size);
            return static_cast<double>(copy[index])/1000.0;
        };
        result.p95Ms=atPercentile(950);
        result.p99Ms=atPercentile(990);
        mSince=nowUs; mFrames=0; mTotalUs=0; mWritten=0;
        return result;
    }
    void reset() noexcept {
        mActive=false; mProjectionVisible=false;
        mLast=mSince=0; mFrames=mTotalUs=mWritten=0;
    }
private:
    bool mActive{};
    bool mProjectionVisible{};
    std::int64_t mLast{},mSince{},mTotalUs{};
    std::size_t mFrames{},mWritten{};
    std::array<std::int64_t,1024> mSamples{};
};
}
