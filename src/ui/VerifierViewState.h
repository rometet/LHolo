#pragma once

#include "structure/SchematicRuntime.h"
#include "structure/VerificationGroups.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <utility>

namespace lholo::ui {
inline std::string verifierSearchText(std::string_view value) {
    std::string result(value);
    for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
inline bool verifierMatchesSearch(structure::Mismatch const& row, std::string_view query) {
    auto const text = verifierSearchText(row.expected + " " + row.actual);
    auto const search = verifierSearchText(query);
    std::size_t at{};
    while (at < search.size()) {
        at = search.find_first_not_of(" \t", at);
        if (at == search.npos) break;
        auto end = search.find_first_of(" \t", at);
        if (end == search.npos) end = search.size();
        if (text.find(search.substr(at, end - at)) == text.npos) return false;
        at = end;
    }
    return true;
}
inline std::pair<std::string_view, std::string_view> verifierBlockParts(std::string_view value) {
    auto const split = value.find(" [");
    return {value.substr(0, split), split == value.npos ? std::string_view{} : value.substr(split + 1)};
}

struct VerifierStateChange {
    std::string name;
    std::optional<std::string> actual, expected;
};
inline std::vector<VerifierStateChange> verifierStateChanges(std::string_view expected, std::string_view actual) {
    auto const parse = [](std::string_view block) {
        std::map<std::string, std::string> values;
        auto state = verifierBlockParts(block).second;
        if (state.starts_with('[')) state.remove_prefix(1);
        if (state.ends_with(']')) state.remove_suffix(1);
        auto const trim = [](std::string_view value) {
            auto const first = value.find_first_not_of(" \t");
            if (first == value.npos) return std::string_view{};
            return value.substr(first, value.find_last_not_of(" \t") - first + 1);
        };
        while (!state.empty()) {
            // Native values are SNBT. A comma inside a quoted string or a
            // nested value is part of that value, not a property separator.
            std::size_t comma = state.npos;
            char quote{};
            bool escaped{};
            int depth{};
            for (std::size_t i = 0; i < state.size(); ++i) {
                auto const c = state[i];
                if (quote) {
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == quote) quote = 0;
                } else if (c == '"' || c == '\'') quote = c;
                else if (c == '[' || c == '{') ++depth;
                else if (c == ']' || c == '}') --depth;
                else if (c == ',' && depth == 0) { comma = i; break; }
            }
            auto const part = trim(state.substr(0, comma));
            auto const equal = part.find('=');
            if (equal != part.npos && !trim(part.substr(0, equal)).empty())
                values.emplace(trim(part.substr(0, equal)), trim(part.substr(equal + 1)));
            if (comma == state.npos) break;
            state.remove_prefix(comma + 1);
        }
        return values;
    };
    auto const wanted = parse(expected), found = parse(actual);
    std::map<std::string, VerifierStateChange> changes;
    for (auto const& [name, value] : wanted) changes[name] = {name, {}, value};
    for (auto const& [name, value] : found) { auto& change = changes[name]; change.name = name; change.actual = value; }
    std::vector<VerifierStateChange> result;
    for (auto const& [name, change] : changes) if (change.actual != change.expected) result.push_back(change);
    return result;
}

// UI-owned transient state. No native pointers, world reads or scan requests.
// Rebuild only when the immutable report, category or search changes.
struct VerifierViewState {
    std::array<char, 192> search{};
    bool errorsOnly{};
    std::weak_ptr<structure::schematic::Report const> cachedReport;
    bool cachedHadReport{};
    std::optional<structure::schematic::ReportStamp> context;
    structure::MistakeFilter cachedFilter{structure::MistakeFilter::Mistakes};
    std::string cachedSearch;
    std::vector<structure::schematic::MismatchGroup> groups;
    std::array<std::vector<std::size_t>, 7> categories;
    std::size_t rebuilds{};

    void update(std::shared_ptr<structure::schematic::Report const> const& report,
        structure::MistakeFilter filter) {
        if (report && context) {
            auto previous = *context, current = report->stamp;
            previous.filterRevision = current.filterRevision = 0;
            if (!previous.sameContext(current)) search.fill(0);
        }
        if (report) context = report->stamp;
        auto const query = std::string{search.data()};
        if (cachedHadReport == static_cast<bool>(report) && cachedReport.lock() == report && cachedFilter == filter && cachedSearch == query) return;
        cachedHadReport = static_cast<bool>(report);
        cachedReport = report; cachedFilter = filter; cachedSearch = query;
        groups.clear(); for (auto& category : categories) category.clear();
        ++rebuilds;
        if (!report || report->running) return;
        auto all = structure::schematic::groupMismatches(report->mismatches, filter);
        for (auto& group : all) {
            auto const& row = report->mismatches[group.indices.front()];
            std::sort(group.indices.begin(), group.indices.end(), [&](auto a, auto b) {
                return structure::nearerMismatch(report->mismatches[a], report->mismatches[b]);
            });
            if (verifierMatchesSearch(row, query))
                categories[static_cast<std::size_t>(row.kind)].push_back(groups.size());
            groups.push_back(std::move(group));
        }
    }
};
} // namespace lholo::ui
