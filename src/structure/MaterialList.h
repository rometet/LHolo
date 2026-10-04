#pragma once
#include "i18n/Message.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lholo::structure::detail {
struct MaterialRequirement {
    std::string displayName;
    std::optional<i18n::TextKey> nameKey;
    std::string typeName;
    std::string itemId;
    std::uint64_t count{};
    int stackSize{64};
    // The tracker carries its existing aggregation key into the UI. Display
    // names are never identities. Body/liquid domains and resolved items stay distinct.
    std::string key;
};
struct MaterialListScope {
    std::uint64_t generation{}, token{};
    bool operator==(MaterialListScope const&) const = default;
};
enum class MaterialFilter : int { All, Shortage, Ignored };
struct MaterialListSnapshot {
    MaterialListScope scope;
    std::uint64_t revision{};
    std::string source;
    std::vector<MaterialRequirement> requirements;
    std::vector<std::optional<int>> available;
    std::set<std::string> ignored;
    bool isIgnored(std::size_t index) const {
        auto const& key = requirements[index].key;
        return !key.empty() && ignored.contains(key);
    }
    std::optional<std::uint64_t> shortage(std::size_t index) const {
        if (index >= available.size() || !available[index]) return std::nullopt;
        auto const count = static_cast<std::uint64_t>(std::max(0, *available[index]));
        return requirements[index].count > count ? requirements[index].count - count : 0;
    }
    bool matches(std::size_t index, MaterialFilter filter) const {
        if (filter == MaterialFilter::Ignored) return isIgnored(index);
        if (filter == MaterialFilter::Shortage) return !isIgnored(index) && shortage(index).value_or(0) > 0;
        return true;
    }
};
inline std::uint64_t materialSum(std::uint64_t a, std::uint64_t b) {
    auto const maximum = std::numeric_limits<std::uint64_t>::max();
    return b > maximum - a ? maximum : a + b;
}
struct MaterialListSummary {
    std::uint64_t original{}, working{}, excluded{};
    std::size_t excludedKinds{}, unknownKinds{};
};
inline MaterialListSummary summarizeMaterials(MaterialListSnapshot const& snapshot) {
    MaterialListSummary result;
    for (std::size_t index = 0; index < snapshot.requirements.size(); ++index) {
        auto const count = snapshot.requirements[index].count;
        result.original = materialSum(result.original, count);
        if (snapshot.isIgnored(index)) {
            result.excluded = materialSum(result.excluded, count); ++result.excludedKinds;
        } else result.working = materialSum(result.working, count);
        if (!snapshot.shortage(index)) ++result.unknownKinds;
    }
    return result;
}
} // namespace lholo::structure::detail
