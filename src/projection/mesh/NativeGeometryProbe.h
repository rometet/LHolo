// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

namespace lholo::projection::detail {
// A bounded, synchronous observer. It never keeps an SDK object, stream view,
// world reference, or GPU pointer. Its render behavior is deliberately empty.
class NativeGeometryProbe {
public:
    static constexpr std::size_t CellLimit=16, VertexLimit=256, FinalVertexLimit=4096;
    bool active{};
    std::uint32_t batch{};
    std::uint64_t generation{};
    std::size_t section{};
    bool select(std::size_t index) noexcept {
        if (!active) return false;
        for(std::size_t i=0;i<mCount;++i) if(mIndices[i]==index) return true;
        if(mCount==CellLimit) return false;
        mIndices[mCount++]=index;return true;
    }
    template<class Position, class Uv, class Colors, class Emit>
    void dump(char const* role, char const* phase, std::size_t index,
        std::string const& body, std::string const& liquid, int layer, int mode,
        bool extraBefore, bool extraAfter, bool returned, std::size_t indexCount,
        Position const& positions, Uv const& uvs, Colors const& colors,
        std::size_t first, std::size_t firstUv, std::size_t firstColor,
        int originX, int originY, int originZ, Emit&& emit) noexcept {
        if(!active) return;
        try {
            if(first>positions.size() || firstUv>uvs.size() || firstColor>colors.size()) return;
            auto const count=positions.size()-first;
            bool const aligned=uvs.size()-firstUv==count;
            bool const colorAligned=colors.size()-firstColor==count;
            auto const limit=std::string_view{phase}=="final" ? FinalVertexLimit : VertexLimit;
            auto const recorded=std::min(count,limit);
            std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(std::numeric_limits<float>::max_digits10);
            auto const emitFloat=[&](float value) { if(std::isfinite(value))s<<value;else s<<"null"; };
            s<<"{\"batch\":"<<batch<<",\"generation\":"<<generation<<",\"section\":"<<section
                <<",\"cell\":"<<index<<",\"role\":"<<std::quoted(role)<<",\"phase\":"<<std::quoted(phase)
                <<",\"body\":"<<std::quoted(body)<<",\"liquid\":"<<std::quoted(liquid)
                <<",\"layer\":"<<layer<<",\"mode\":"<<mode<<",\"extra_before\":"<<extraBefore
                <<",\"extra_after\":"<<extraAfter<<",\"returned\":"<<returned<<",\"index_count\":"<<indexCount
                <<",\"first\":"<<first<<",\"total\":"<<count<<",\"aligned\":"<<aligned
                <<",\"color_aligned\":"<<colorAligned<<",\"recorded\":"<<recorded<<",\"origin\":["<<originX<<','<<originY<<','<<originZ<<"],\"vertices\":[";
            for(std::size_t i=0;i<recorded;++i) {
                auto const& p=positions[first+i];
                if(i)s<<',';
                s<<'[';emitFloat(p.x);s<<',';emitFloat(p.y);s<<',';emitFloat(p.z);s<<',';
                if(aligned) { auto const& uv=uvs[firstUv+i];emitFloat(uv.x);s<<',';emitFloat(uv.y); }
                else s<<"null,null";
                s<<',';
                if(colorAligned)s<<colors[firstColor+i];else s<<"null";
                s<<']';
            }
            s<<"]}";emit(s.str());
        } catch(...) { /* Observation must never change native mesh ownership. */ }
    }
private:
    std::array<std::size_t,CellLimit> mIndices{};
    std::size_t mCount{};
};
inline thread_local NativeGeometryProbe* gActiveNativeGeometryProbe{};
class ScopedNativeGeometryProbe {
public:
    explicit ScopedNativeGeometryProbe(NativeGeometryProbe& probe) noexcept
        : mPrevious(gActiveNativeGeometryProbe) { gActiveNativeGeometryProbe=&probe; }
    ~ScopedNativeGeometryProbe() { gActiveNativeGeometryProbe=mPrevious; }
    ScopedNativeGeometryProbe(ScopedNativeGeometryProbe const&)=delete;
    ScopedNativeGeometryProbe& operator=(ScopedNativeGeometryProbe const&)=delete;
private:
    NativeGeometryProbe* mPrevious{};
};
} // namespace lholo::projection::detail
