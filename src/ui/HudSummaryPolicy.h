#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
namespace lholo::ui {
struct HudProgress {std::optional<float> ratio;std::uint64_t remaining{};};
inline HudProgress hudProgress(std::uint64_t placed,std::uint64_t total)noexcept {
    if(!total)return {};
    placed=(std::min)(placed,total);
    return {static_cast<float>(static_cast<double>(placed)/static_cast<double>(total)),total-placed};
}
struct MissingTotal {
    std::uint64_t value{};bool exact{true};
    void add(std::uint64_t amount)noexcept {
        if(!exact)return;
        if(amount>(std::numeric_limits<std::uint64_t>::max)()-value){exact=false;return;}
        value+=amount;
    }
};
}
