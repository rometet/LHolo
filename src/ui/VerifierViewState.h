#pragma once

#include "structure/SchematicRuntime.h"
#include "structure/VerificationGroups.h"
#include <algorithm>
#include <array>
#include <cctype>
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
