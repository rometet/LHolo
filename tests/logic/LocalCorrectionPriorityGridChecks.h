#pragma once
#include "projection/mesh/LocalCorrectionPriorityGrid.h"
#include "projection/mesh/SectionBlockSnapshot.h"
#include "projection/mesh/CorrectionFaceMask.h"
#include "projection/correction/CorrectionStateStore.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace lholo::tests {
template <class Check>
void runLocalCorrectionPriorityGridChecks(Check check) {
    using lholo::projection::detail::LocalCorrectionPriorityGrid;
    using lholo::projection::detail::SectionBlockSnapshot;

    LocalCorrectionPriorityGrid g;
    check(g.reset(-8, -3, 10, 9, 14, 27)); // 18^3 halo
    check(g.bytes() == 18 * 18 * 18);
    check(g.get(-9, 0, 12) == 0);
    check(g.get(-8, -3, 10) == 0);
    g.add(-8, -3, 10, 1);
    g.add(-8, -3, 10, 2); // wrong/extra supersedes missing
    g.add(-8, -3, 10, 1);
    check(g.get(-8, -3, 10) == 2);
    g.add(9, 14, 27, 1);
    check(g.get(9, 14, 27) == 1);
    g.add(-9, -3, 10, 2);
    check(g.get(-8, -3, 10) == 2);
    check(!g.reset(0, 0, 0, 32, 0, 0)); // sparse/invalid layout: fallback
    check(g.bytes() == 0);
    check(!g.reset(0, 0, 0, -1, 0, 0));
    check(g.reset(0, 0, 0, 0, 0, 0));
    g.add(0, 0, 0, 1);
    check(g.get(0, 0, 0) == 1);

    std::vector<std::uint8_t> corrections(50000), actors(50000);
    for (std::size_t i = 0; i < corrections.size(); ++i) {
        corrections[i] = static_cast<std::uint8_t>(i % 5);
        actors[i] = static_cast<std::uint8_t>(i % 2);
    }
    SectionBlockSnapshot snapshot;
    for (int fixture=0; fixture<3; ++fixture) {
        std::vector<std::size_t> indices;
        for (std::size_t i=0; i<corrections.size(); ++i) {
            if (fixture==0 ? i>=16384 && i<20480
                : fixture==1 ? (i%311)==0 : false) indices.push_back(i);
        }
        snapshot.capture(indices,corrections,actors);
        std::size_t traversed=0;
        std::size_t last=0;
        snapshot.forEach([&](std::size_t index, SectionBlockSnapshot::Value const& value){
            if (traversed) check(index>last);
            check(value.correction==corrections[index]);
            check(value.actorRenderer==actors[index]);
            last=index;
            ++traversed;
        });
        check(traversed==indices.size());
    }

    // Single-layer correction fill must not be occluded by a hidden block
    // above it. Capture both pages to reproduce the worker's 6-neighbor
    // section snapshot, including a face across a subchunk boundary (Y=15/16).
    // Raw truth remains Missing/WrongType even when the render view is hidden.
    {
        using namespace lholo::projection::detail;
        std::vector<std::vector<std::size_t>> pages{{0}, {1}};
        CorrectionStateStore states;
        states.initialize(pages, 2);
        states.set(1, CorrectionState::WrongType);
        std::array<std::size_t, 2> capturedSections{0, 1};
        for (bool upperVisible : {false, true}) {
            states.setVisible(1, upperVisible);
            auto view = states.capture(capturedSections);
            check(view.renderState(0) == CorrectionState::Missing);
            check(states.get(1) == CorrectionState::WrongType);
            check(view.renderState(1) == (upperVisible ? CorrectionState::WrongType : CorrectionState::Correct));
            LocalCorrectionPriorityGrid surface;
            check(surface.reset(0, 14, 0, 0, 17, 0));
            surface.add(0, 15, 0, static_cast<std::uint8_t>(correctionPriority(view.renderState(0))));
            surface.add(0, 16, 0, static_cast<std::uint8_t>(correctionPriority(view.renderState(1))));
            auto top = correctionExposedFaceMask(correctionPriority(view.renderState(0)),
                [&](int dx, int dy, int dz) { return static_cast<int>(surface.get(dx, 15 + dy, dz)); });
            // Face bit 5 is +Y. A hidden upper block must never hide it.
            check(((top >> 5) & 1U) == (upperVisible ? 0U : 1U));
            check((top & 0x1FU) == 0x1FU); // sides/bottom remain exposed
        }
        states.setVisible(1, true);
        states.set(1, CorrectionState::Missing);
        auto allLayers = states.capture(capturedSections);
        LocalCorrectionPriorityGrid surface;
        check(surface.reset(0, 14, 0, 0, 17, 0));
        surface.add(0, 16, 0, static_cast<std::uint8_t>(correctionPriority(allLayers.renderState(1))));
        check((correctionExposedFaceMask(1, [&](int dx, int dy, int dz) {
            return static_cast<int>(surface.get(dx, 15 + dy, dz));
        }) & (1U << 5)) == 0);
    }

    // Deterministic random priorities: the dense grid must agree with the
    // reference sparse lookup, including missing neighbors and extra cells.
    check(g.reset(0, 0, 0, 17, 17, 17));
    std::vector<std::uint8_t> reference(18*18*18,0);
    auto idx=[](int x,int y,int z){return (x*18+y)*18+z;};
    std::uint32_t seed=0x12345678U;
    for(int i=0;i<10000;++i){
        seed=seed*1664525U+1013904223U;
        int x=static_cast<int>((seed>>8)%20)-1;
        seed=seed*1664525U+1013904223U;
        int y=static_cast<int>((seed>>8)%20)-1;
        seed=seed*1664525U+1013904223U;
        int z=static_cast<int>((seed>>8)%20)-1;
        auto priority=static_cast<std::uint8_t>((seed>>24)%3);
        g.add(x,y,z,priority);
        if(x>=0&&x<18&&y>=0&&y<18&&z>=0&&z<18)
            reference[idx(x,y,z)]=(std::max)(reference[idx(x,y,z)],priority);
    }
    for(int x=-1;x<=18;++x)
      for(int y=-1;y<=18;++y)
        for(int z=-1;z<=18;++z){
            auto expected=(x>=0&&x<18&&y>=0&&y<18&&z>=0&&z<18)
                ? reference[idx(x,y,z)] : 0;
            check(g.get(x,y,z)==expected);
        }
}
} // namespace lholo::tests
