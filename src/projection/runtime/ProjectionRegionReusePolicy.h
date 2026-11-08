#pragma once
#include <cstddef>
#include <set>
#include <tuple>
#include <vector>

namespace lholo::projection::detail {

// Compare sorted source-local cells and return 16^3 section keys affected by
// an edit, including the conservative one-section neighbor halo. No GPU or
// world references are needed, making this safe to regression test.
template<class OldCells, class NextCells, class SameCell>
std::set<std::tuple<int,int,int>> changedRegionSectionHalo(
    OldCells const& oldCells,
    NextCells const& nextCells,
    SameCell sameCell) {
    using Cell=typename OldCells::value_type;
    std::set<std::tuple<int,int,int>> changed;
    std::set<std::tuple<int,int,int>> sourceSectionsAlreadyMarked;
    auto const mark=[&](Cell const& cell) {
        auto const x=cell.x/16,y=cell.y/16,z=cell.z/16;
        // An added region can have thousands of cells per section; expand
        // its 26-neighbor halo exactly once instead of per changed cell.
        if(!sourceSectionsAlreadyMarked.emplace(x,y,z).second)return;
        for(int dx=-1;dx<=1;++dx)
            for(int dy=-1;dy<=1;++dy)
                for(int dz=-1;dz<=1;++dz)
                    changed.emplace(x+dx,y+dy,z+dz);
    };
    auto less=[](Cell const& a,Cell const& b){
        return std::tie(a.x,a.y,a.z)<std::tie(b.x,b.y,b.z);
    };
    std::size_t i{},j{};
    while(i<oldCells.size() || j<nextCells.size()) {
        if(j==nextCells.size() ||
           (i<oldCells.size() && less(oldCells[i],nextCells[j])))
            mark(oldCells[i++]);
        else if(i==oldCells.size() || less(nextCells[j],oldCells[i]))
            mark(nextCells[j++]);
        else {
            if(!sameCell(oldCells[i],nextCells[j])) mark(nextCells[j]);
            ++i;++j;
        }
    }
    return changed;
}

} // namespace lholo::projection::detail
