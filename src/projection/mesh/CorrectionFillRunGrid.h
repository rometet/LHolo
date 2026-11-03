// Exact 1D/2D union of coplanar correction-fill faces with equal color.
// Only used for dense all-Missing sections, never across correction categories.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lholo::projection::detail {
class CorrectionFillRunGrid {
public:
    bool reset(int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
        auto const x = static_cast<std::int64_t>(maxX) - minX + 1;
        auto const y = static_cast<std::int64_t>(maxY) - minY + 1;
        auto const z = static_cast<std::int64_t>(maxZ) - minZ + 1;
        // Source sections are at most 16x16x16 (under 90-degree rotations).
        // Unsupported/sparse layouts retain the original per-face fallback.
        if (x < 1 || y < 1 || z < 1 || x > 16 || y > 16 || z > 16) {
            mFaces.clear();
            mX = mY = mZ = 0;
            return false;
        }
        mMinX = minX; mMinY = minY; mMinZ = minZ;
        mX = static_cast<int>(x);
        mY = static_cast<int>(y);
        mZ = static_cast<int>(z);
        mFaces.assign(static_cast<std::size_t>(mX * mY * mZ), 0);
        return true;
    }

    void add(int x, int y, int z, std::uint8_t exposedFaces) noexcept {
        if (x < mMinX || y < mMinY || z < mMinZ) return;
        auto const dx = static_cast<std::int64_t>(x) - mMinX;
        auto const dy = static_cast<std::int64_t>(y) - mMinY;
        auto const dz = static_cast<std::int64_t>(z) - mMinZ;
        if (dx >= mX || dy >= mY || dz >= mZ) return;
        mFaces[index(static_cast<int>(dx), static_cast<int>(dy),
                     static_cast<int>(dz))] |= exposedFaces;
    }

    // For each emitted strip: face index 0..5, first unit cell origin and
    // number of adjacent unit faces in +X (0,1,4,5) or +Z (2,3).
    // Output covers exactly the union of input unit faces; no culling.
    template <class Emit>
    void forEachRun(Emit&& emit) const {
        if (mFaces.empty()) return;
        for (int side = 0; side < 6; ++side) {
            auto const bit = static_cast<std::uint8_t>(1u << side);
            if (side == 2 || side == 3) {
                for (int x = 0; x < mX; ++x)
                    for (int y = 0; y < mY; ++y) {
                        for (int z = 0; z < mZ;) {
                            if (!(mFaces[index(x,y,z)] & bit)) { ++z; continue; }
                            int length = 1;
                            while (z + length < mZ
                                && (mFaces[index(x,y,z+length)] & bit)) ++length;
                            emit(side, mMinX+x, mMinY+y, mMinZ+z, length);
                            z += length;
                        }
                    }
            } else {
                for (int z = 0; z < mZ; ++z)
                    for (int y = 0; y < mY; ++y) {
                        for (int x = 0; x < mX;) {
                            if (!(mFaces[index(x,y,z)] & bit)) { ++x; continue; }
                            int length = 1;
                            while (x + length < mX
                                && (mFaces[index(x+length,y,z)] & bit)) ++length;
                            emit(side, mMinX+x, mMinY+y, mMinZ+z, length);
                            x += length;
                        }
                    }
            }
        }
    }


    // Exact 2D greedy union for each co-planar face orientation. Rectangles
    // never cross the input section bounds or different correction colors.
    // side: -Z,+Z,-X,+X,-Y,+Y; width is +X or +Z, height is +Y or +Z.
    template<class Emit>
    void forEachRectangle(Emit&& emit) const {
        if (mFaces.empty()) return;
        for (int side=0; side<6; ++side) {
            auto const bit = static_cast<std::uint8_t>(1u << side);
            auto const sideZ = side == 0 || side == 1;
            auto const sideX = side == 2 || side == 3;
            auto const planes = sideZ ? mZ : (sideX ? mX : mY);
            auto const dimU = sideX ? mZ : mX;
            auto const dimV = sideZ || sideX ? mY : mZ;
            for (int plane=0; plane<planes; ++plane) {
                // Max 16x16 visited slots per 2D plane. Do not mutate the
                // source face bits; unit-face equivalence stays testable.
                std::array<std::uint8_t, 256> visited{};
                auto const indexAt = [&](int u, int v) {
                    int const x = sideX ? plane : u;
                    int const y = sideZ || sideX ? v : plane;
                    int const z = sideZ ? plane : (sideX ? u : v);
                    return index(x,y,z);
                };
                auto const usable = [&](int u, int v) {
                    auto const offset = static_cast<std::size_t>(v*dimU+u);
                    return !(visited[offset])
                        && (mFaces[indexAt(u,v)] & bit) != 0;
                };
                for (int v=0; v<dimV; ++v) {
                    for (int u=0; u<dimU; ++u) {
                        if (!usable(u,v)) continue;
                        int width = 1;
                        while (u+width < dimU && usable(u+width,v)) ++width;
                        int height=1;
                        while (v+height < dimV) {
                            bool all = true;
                            for (int k=0; k<width; ++k) {
                                if (!usable(u+k,v+height)) { all=false; break; }
                            }
                            if (!all) break;
                            ++height;
                        }
                        for (int dv=0; dv<height; ++dv)
                            for (int du=0; du<width; ++du)
                                visited[static_cast<std::size_t>((v+dv)*dimU+u+du)]=1;
                        int const x = sideX ? plane : u;
                        int const y = sideZ || sideX ? v : plane;
                        int const z = sideZ ? plane : (sideX ? u : v);
                        emit(side,mMinX+x,mMinY+y,mMinZ+z,width,height);
                    }
                }
            }
        }
    }

private:
    std::size_t index(int x, int y, int z) const noexcept {
        return (static_cast<std::size_t>(x)*static_cast<std::size_t>(mY)
                + static_cast<std::size_t>(y))*static_cast<std::size_t>(mZ)
                + static_cast<std::size_t>(z);
    }
    int mMinX{}, mMinY{}, mMinZ{}, mX{}, mY{}, mZ{};
    std::vector<std::uint8_t> mFaces;
};
} // namespace lholo::projection::detail
