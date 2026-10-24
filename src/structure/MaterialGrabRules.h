#pragma once
// Pure and bounded material-grabbing rules. No game/packet ownership here.
#include "structure/MaterialList.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace lholo::structure::detail {
using MaterialGrabBudget = std::map<std::string, std::uint64_t>;
inline MaterialGrabBudget computeGrabBudget(
    std::vector<MaterialRequirement> const& rows,
    std::map<std::string,int> const& inventory,
    bool countsComplete
) {
    MaterialGrabBudget budget;
    if (!countsComplete) return budget; // malformed shulker NBT: fail closed
    for (auto const& row : rows) {
        if (row.itemId.empty() || row.count == 0
            || row.itemId == "minecraft:water" || row.itemId == "minecraft:lava") continue;
        auto& total=budget[row.itemId];
        auto const room=std::numeric_limits<std::uint64_t>::max()-total;
        total+=std::min(room,row.count);
    }
    for (auto it=budget.begin();it!=budget.end();) {
        auto const have=inventory.find(it->first);
        auto const carried=have==inventory.end()?0:std::max(0,have->second);
        if (it->second<=static_cast<std::uint64_t>(carried))
            it=budget.erase(it);
        else {
            it->second-=static_cast<std::uint64_t>(carried);
            ++it;
        }
    }
    return budget;
}
inline int grabAmountForSlot(MaterialGrabBudget const& budget,
    std::string_view itemId,int stackCount,int maxStack=64) {
    if(stackCount<=0 || maxStack<=0 || itemId.empty())return 0;
    auto it=budget.find(std::string{itemId});
    if(it==budget.end()) return 0;
    auto const cap=static_cast<std::uint64_t>(std::min(stackCount,maxStack));
    return static_cast<int>(std::min(cap,it->second));
}
}
