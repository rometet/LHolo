#pragma once

#include "structure/PlacementTransform.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lholo::structure {
enum class VerificationState : std::uint8_t { Ignored, Unknown, Correct, Missing, WrongType, WrongState, Extra };
inline bool placeholderBlock(std::string_view name) {
    return name=="minecraft:unknown" || name=="minecraft:info_update" || name=="minecraft:info_update2";
}
inline VerificationState combineExpectedStates(VerificationState a,VerificationState b) {
    for(auto state:{VerificationState::Unknown,VerificationState::Missing,VerificationState::WrongType,VerificationState::WrongState})
        if(a==state || b==state)return state;
    return a==VerificationState::Ignored?b:a;
}
inline VerificationState classifyCell(bool covered, bool ready, bool expectedAir,
    bool actualAir, bool sameType, bool sameState, bool countExtras) {
    if (!covered) return VerificationState::Ignored;
    if (!ready) return VerificationState::Unknown;
    if (expectedAir) return actualAir ? VerificationState::Correct
        : countExtras ? VerificationState::Extra : VerificationState::Ignored;
    if (actualAir) return VerificationState::Missing;
    if (!sameType) return VerificationState::WrongType;
    return sameState ? VerificationState::Correct : VerificationState::WrongState;
}
struct VerificationTally {
    std::uint64_t correct{}, missing{}, wrongType{}, wrongState{}, extra{}, unknown{}, unknownAir{};
    std::uint64_t total() const { return correct + missing + wrongType + wrongState + unknown; }
    void add(VerificationState state, bool expectsBlock) {
        switch (state) {
        case VerificationState::Correct: if (expectsBlock) ++correct; break;
        case VerificationState::Unknown: if (expectsBlock) ++unknown; else ++unknownAir; break;
        case VerificationState::Missing: ++missing; break;
        case VerificationState::WrongType: ++wrongType; break;
        case VerificationState::WrongState: ++wrongState; break;
        case VerificationState::Extra: ++extra; break;
        default: break;
        }
    }
};
enum class MistakeFilter : int { Mistakes, WrongAndExtra, WrongState, Missing };
inline bool matchesFilter(VerificationState state, MistakeFilter filter) {
    switch (filter) {
    case MistakeFilter::WrongAndExtra: return state == VerificationState::WrongType || state == VerificationState::Extra;
    case MistakeFilter::WrongState: return state == VerificationState::WrongState;
    case MistakeFilter::Missing: return state == VerificationState::Missing;
    default: return state == VerificationState::Missing || state == VerificationState::WrongType
        || state == VerificationState::WrongState || state == VerificationState::Extra;
    }
}
inline char const* verificationStateName(VerificationState state) {
    constexpr std::array names{"Ignored", "Unknown", "Correct", "Missing", "WrongType", "WrongState", "Extra"};
    return names[static_cast<std::size_t>(state)];
}
struct Mismatch {
    VerificationState kind{};
    Cell world;
    std::string expected, actual;
    double distanceSquared{};
};
inline bool nearerMismatch(Mismatch const& a, Mismatch const& b) {
    if (a.distanceSquared != b.distanceSquared) return a.distanceSquared < b.distanceSquared;
    if (a.world.x != b.world.x) return a.world.x < b.world.x;
    if (a.world.y != b.world.y) return a.world.y < b.world.y;
    if (a.world.z != b.world.z) return a.world.z < b.world.z;
    return a.kind < b.kind;
}
inline bool retainNearest(std::vector<Mismatch>& heap,Mismatch row,std::size_t cap) {
    if(!cap)return true;
    if(heap.size()<cap){heap.push_back(std::move(row));std::push_heap(heap.begin(),heap.end(),nearerMismatch);return false;}
    if(nearerMismatch(row,heap.front())){
        std::pop_heap(heap.begin(),heap.end(),nearerMismatch);heap.back()=std::move(row);std::push_heap(heap.begin(),heap.end(),nearerMismatch);
    }
    return true;
}
// Unique source-region cells, including air. Every region step, emitted cell
// and overlap comparison consumes budget, so even thousands of overlapping
// litematic regions cannot monopolize a tick. Gaps never become expected air.
struct RegionScanCursor {
    std::size_t region{}, prior{};
    std::uint64_t index{};
    std::optional<Cell> candidate;
    template<class Regions>
    std::optional<Cell> next(Regions const& regions,std::size_t& budget) {
        while(budget && region<regions.size()) {
            --budget;
            if(candidate) {
                if(prior<region){
                    auto const& b=regions[prior++];auto const p=*candidate;
                    if(p.x>=b.x && p.y>=b.y && p.z>=b.z && p.x<static_cast<std::int64_t>(b.x)+b.sizeX
                        && p.y<static_cast<std::int64_t>(b.y)+b.sizeY && p.z<static_cast<std::int64_t>(b.z)+b.sizeZ)candidate.reset();
                    continue;
                }
                auto p=candidate;candidate.reset();return p;
            }
            auto const& b=regions[region];
            if(b.sizeX<=0 || b.sizeY<=0 || b.sizeZ<=0){++region;index=0;continue;}
            auto const yz=static_cast<std::uint64_t>(b.sizeY)*b.sizeZ;
            auto const volume=static_cast<std::uint64_t>(b.sizeX)*yz;
            if(index>=volume){++region;index=0;continue;}
            auto const n=index++;candidate=Cell{b.x+static_cast<std::int64_t>(n/yz),b.y+static_cast<std::int64_t>((n%yz)/b.sizeZ),b.z+static_cast<std::int64_t>(n%b.sizeZ)};prior=0;
        }
        return std::nullopt;
    }
};
// A bounded, deterministic cycle over an already ordered result. No world
// scanning occurs in the action/UI; the game-tick job populates this cache.
template<class Range>
std::optional<std::size_t> nextMistake(Range const& rows, MistakeFilter filter, std::optional<std::size_t> previous) {
    auto const count = rows.size();
    auto const start = previous && *previous < count ? (*previous + 1) % count : 0;
    for (std::size_t i = 0; i < count; ++i) {
        auto const index = (start + i) % count;
        if (matchesFilter(rows[index].kind, filter)) return index;
    }
    return std::nullopt;
}
struct RequiredItemRule {
    std::string_view alias; // empty: use existing native block->item resolver
    unsigned quantity{1};
};
inline RequiredItemRule requiredItemRule(std::string_view name, bool upperDoor = false, bool bedHead = false, bool ownerVisible = true) {
    if (upperDoor || bedHead) return {{}, ownerVisible ? 0u : 1u};
    if (name == "minecraft:wall_torch") return {"minecraft:torch", 1};
    if (name == "minecraft:redstone_wall_torch") return {"minecraft:redstone_torch", 1};
    if (name == "minecraft:soul_wall_torch") return {"minecraft:soul_torch", 1};
    // Bedrock has separate double-slab block types. Native item conversion
    // retains the wood/stone/aux variant; quantity is a separate pure rule.
    if (name.find("double") != name.npos && name.find("slab") != name.npos) return {{}, 2};
    return {};
}
struct MaterialCount {
    std::uint64_t total{}, correct{};
    std::uint64_t remaining() const { return total - (std::min)(total, correct); }
    void add(unsigned quantity, VerificationState state) {
        total += quantity;
        if (state == VerificationState::Correct) correct += quantity;
    }
};
} // namespace lholo::structure
