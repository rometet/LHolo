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

// One bounded, server-authoritative batch: unique source and empty destination
// slots; reduce the shared missing-material budget as each action is selected.
struct GrabBatchSource {int slot{}; std::string itemId; int amount{};};
struct GrabBatchMove {int sourceSlot{},destinationSlot{},amount{}; std::string itemId;};
inline std::vector<GrabBatchMove> planGrabBatch(
    MaterialGrabBudget remaining,
    std::vector<GrabBatchSource> const& sources,
    std::vector<int> const& emptyDestinations,
    std::size_t maxActions=36
) {
    std::vector<GrabBatchMove> result;
    maxActions=std::min<std::size_t>(maxActions,36);
    bool usedSource[108]{};
    bool usedDestination[36]{};
    std::size_t cursor{};
    for(auto const& source:sources) {
        if(result.size()>=maxActions)break;
        if(source.slot<0 || source.slot>=108 || usedSource[source.slot]
            || source.amount<=0 || source.itemId.empty())continue;
        const int amount=grabAmountForSlot(remaining,source.itemId,source.amount);
        if(amount<=0)continue;
        while(cursor<emptyDestinations.size()) {
            const int dest=emptyDestinations[cursor++];
            if(dest<0 || dest>=36 || usedDestination[dest])continue;
            usedSource[source.slot]=true;
            usedDestination[dest]=true;
            result.push_back({source.slot,dest,amount,source.itemId});
            auto budget=remaining.find(source.itemId);
            if(budget!=remaining.end())budget->second-=static_cast<std::uint64_t>(amount);
            break;
        }
        if(cursor>=emptyDestinations.size())break;
    }
    return result;
}
} // namespace lholo::structure::detail
