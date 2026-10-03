#pragma once
#include "structure/Verification.h"
#include <map>
#include <span>
#include <tuple>

namespace lholo::structure::schematic {
struct MismatchGroup { std::vector<std::size_t> indices; };
// Counts describe the retained rows; category totals still come from the full
// scan tally. Indices always address the immutable report used for selection.
inline std::vector<MismatchGroup> groupMismatches(std::span<Mismatch const> rows,MistakeFilter filter) {
    std::map<std::tuple<VerificationState,std::string,std::string>,std::size_t> keys;
    std::vector<MismatchGroup> groups;
    for(std::size_t i=0;i<rows.size();++i) {
        auto const& row=rows[i];
        if(!matchesFilter(row.kind,filter))continue;
        auto const [it,added]=keys.emplace(std::tuple{row.kind,row.expected,row.actual},groups.size());
        if(added)groups.emplace_back();
        groups[it->second].indices.push_back(i);
    }
    return groups;
}
} // namespace lholo::structure::schematic
