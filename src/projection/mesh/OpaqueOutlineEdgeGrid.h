#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lholo::projection::detail {
class OpaqueOutlineEdgeGrid {
public:
    bool reset(std::int64_t x0, std::int64_t y0, std::int64_t z0,
               std::int64_t x1, std::int64_t y1, std::int64_t z1) {
        mMasks.clear();
        if (x0 > x1 || y0 > y1 || z0 > z1
            || x0 < -16000000 || y0 < -16000000 || z0 < -16000000
            || x1 > 16000000 || y1 > 16000000 || z1 > 16000000)
            return false;
        auto dx=x1-x0+2, dy=y1-y0+2, dz=z1-z0+2;
        if (dx<2 || dy<2 || dz<2 || dx>20 || dy>20 || dz>20)
            return false;
        mX=x0; mY=y0; mZ=z0;
        sx=static_cast<std::size_t>(dx);
        sy=static_cast<std::size_t>(dy);
        sz=static_cast<std::size_t>(dz);
        mMasks.assign(sx*sy*sz,0);
        return true;
    }
    bool claim(int axis, std::int64_t x, std::int64_t y, std::int64_t z,
               std::uint8_t colorBit) noexcept {
        if (axis<0 || axis>2 || colorBit==0 || colorBit>8
            || (colorBit&(colorBit-1))!=0 || mMasks.empty()) return true;
        auto const ix=x-mX, iy=y-mY, iz=z-mZ;
        if (ix<0 || iy<0 || iz<0 ||
            static_cast<std::uint64_t>(ix)>=sx ||
            static_cast<std::uint64_t>(iy)>=sy ||
            static_cast<std::uint64_t>(iz)>=sz) return true;
        auto const offset=(static_cast<std::size_t>(ix)*sy
            +static_cast<std::size_t>(iy))*sz+static_cast<std::size_t>(iz);
        auto const bit=static_cast<std::uint16_t>(colorBit)<<(axis*4);
        if ((mMasks[offset]&bit)!=0) return false;
        mMasks[offset]|=bit;
        return true;
    }
    // Produce maximal straight runs of occupied unit edges for one color.
    // Runs have the same geometric coverage as their constituent lines.
    // The caller decides which opaque visual layer is safe to coalesce.
    template<class Emit>
    std::size_t forEachMergedRun(std::uint8_t colorBit, Emit&& emit) const {
        if (mMasks.empty() || colorBit==0 || colorBit>8
            || (colorBit&(colorBit-1))!=0) return 0;
        std::array<std::size_t,3> const dims{sx,sy,sz};
        std::size_t runs=0;
        for (std::size_t axis=0; axis<3; ++axis) {
            auto const otherA=(axis+1)%3, otherB=(axis+2)%3;
            auto const bit=static_cast<std::uint16_t>(colorBit) << (axis*4);
            std::array<std::size_t,3> pos{};
            for(std::size_t a=0;a<dims[otherA];++a) {
                pos[otherA]=a;
                for(std::size_t b=0;b<dims[otherB];++b) {
                    pos[otherB]=b;
                    for(std::size_t t=0;t<dims[axis];) {
                        pos[axis]=t;
                        auto const offset=(pos[0]*sy+pos[1])*sz+pos[2];
                        if ((mMasks[offset]&bit)==0) {++t;continue;}
                        auto const start=t;
                        do {
                            ++t;
                            if(t==dims[axis]) break;
                            pos[axis]=t;
                        } while ((mMasks[(pos[0]*sy+pos[1])*sz+pos[2]]&bit)!=0);
                        pos[axis]=start;
                        emit(static_cast<int>(axis),
                             mX+static_cast<std::int64_t>(pos[0]),
                             mY+static_cast<std::int64_t>(pos[1]),
                             mZ+static_cast<std::int64_t>(pos[2]),t-start);
                        ++runs;
                    }
                }
            }
        }
        return runs;
    }
    [[nodiscard]] bool enabled() const noexcept {return !mMasks.empty();}
    [[nodiscard]] std::size_t bytes() const noexcept {
        return mMasks.size()*sizeof(std::uint16_t);
    }
    [[nodiscard]] std::size_t maximumVertexCount(std::size_t colorCount) const noexcept {
        return mMasks.size()*6U*colorCount;
    }
private:
    std::int64_t mX{},mY{},mZ{};
    std::size_t sx{},sy{},sz{};
    std::vector<std::uint16_t> mMasks;
};
} // namespace lholo::projection::detail
