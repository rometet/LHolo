// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// PORTED FROM LeviSchematic (LGPL-3.0, tag v1.2.0, commit e6f803b) and adapted
// to LHolo's material snapshot model:
//   src/levischematic/interaction/survival/inventory/SurvivalAlgorithms.h
//     ::levischematic::survival::csvCell
//     ::levischematic::survival::MaterialCounts
//   src/levischematic/interaction/survival/service/MaterialService.cpp
//     MaterialService::add / ::exportReport (CSV + JSON column sets, BOM, CRLF)
// See THIRD_PARTY_NOTICES.md and licenses/LeviSchematic/LICENSE.
//
// Differences from upstream, recorded for AGPL/GPL provenance honesty:
//   * upstream carries four shortfall buckets (missing / wrong_block /
//     wrong_state / unknown) because its verifier scans the live world. LHolo's
//     material snapshot is inventory-only, so the buckets collapse into
//     "total" and "missing" (== inventory shortage). No column is fabricated.
//   * upstream multiplies totals by a user-supplied build multiplier; the same
//     integer multiplier is supported here and applies to totals and shortages
//     while carried/available stays unscaled, matching upstream exactly.
//   * rendering is to std::string rather than a file so the rules are unit
//     testable and the existing atomic writer keeps ownership of IO.

#pragma once

#include "structure/MaterialList.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace lholo::structure::material_rules {

// Shortfall buckets mirroring upstream survival::MaterialCounts, reduced to the
// two buckets LHolo can actually observe.
struct MaterialShortfall {
    std::uint64_t total{};
    std::uint64_t missing{};
};

[[nodiscard]] inline std::uint64_t saturatingScale(std::uint64_t value, std::uint64_t multiplier) {
    if (multiplier == 0) return 0;
    if (value > std::numeric_limits<std::uint64_t>::max() / multiplier)
        return std::numeric_limits<std::uint64_t>::max();
    return value * multiplier;
}

// RFC4180 quoted CSV cell, identical to upstream survival::csvCell.
[[nodiscard]] inline std::string csvCell(std::string_view value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') result += '"';
        result += c;
    }
    return result + '"';
}

// Spreadsheet formula guard for CSV text cells, matching MaterialExport.cpp's
// TSV `field`: a leading =, +, -, @, tab, CR or LF is neutralised with an
// apostrophe BEFORE RFC4180 quoting, so the quote itself cannot be interpreted
// as a formula. JSON deliberately keeps the original text.
[[nodiscard]] inline std::string csvTextCell(std::string_view value) {
    if (!value.empty() && (value[0] == '=' || value[0] == '+' || value[0] == '-' || value[0] == '@'
                           || value[0] == '\t' || value[0] == '\r' || value[0] == '\n')) {
        std::string guarded{"'"};
        guarded.append(value);
        return csvCell(guarded);
    }
    return csvCell(value);
}

// JSON string escaping equivalent to upstream MaterialService::exportReport's
// local `quote` lambda: control characters are dropped, quote and backslash are
// backslash-escaped, and the value is always double quoted.
[[nodiscard]] inline std::string jsonQuote(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        if (static_cast<unsigned char>(c) < 0x20) continue;
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + '"';
}

struct JeExportOptions {
    // Display name per requirement, already localized by the caller. When it is
    // empty the requirement's own displayName is used.
    std::vector<std::string> const* names{};
    std::uint64_t multiplier{1};
    // Row selection mirrors the TSV writer. All/Shortage keep excluding ignored
    // rows; Ignored exports exactly the explicitly ignored rows.
    detail::MaterialFilter filter{detail::MaterialFilter::All};
};

// Emits the upstream CSV column set and header ordering:
//   item,aux,label,total,missing,wrong_block,wrong_state,unknown,available,to_gather
// Columns LHolo cannot observe (wrong_block, wrong_state, unknown) are written as
// 0 so downstream JE material-list tooling can parse the file unchanged.
[[nodiscard]] inline std::string renderJeCsv(
    detail::MaterialListSnapshot const& snapshot,
    JeExportOptions const& options = {}
) {
    auto const multiplier = options.multiplier == 0 ? 1u : options.multiplier;
    std::string data = "\xEF\xBB\xBFitem,aux,label,total,missing,wrong_block,wrong_state,unknown,available,to_gather\r\n";
    auto label = [&](std::size_t index) -> std::string const& {
        if (options.names && index < options.names->size() && !(*options.names)[index].empty())
            return (*options.names)[index];
        return snapshot.requirements[index].displayName;
    };
    for (std::size_t index = 0; index < snapshot.requirements.size(); ++index) {
        if (!snapshot.matches(index, options.filter)) continue;
        if (options.filter != detail::MaterialFilter::Ignored && snapshot.isIgnored(index)) continue;
        auto const& row = snapshot.requirements[index];
        MaterialShortfall counts;
        counts.total   = row.count;
        counts.missing = snapshot.shortage(index).value_or(row.count);
        std::uint64_t const available = index < snapshot.available.size() && snapshot.available[index]
            ? static_cast<std::uint64_t>(std::max(0, *snapshot.available[index]))
            : 0u;
        std::uint64_t const remaining = counts.missing;
        std::uint64_t const toGather  = remaining > available ? remaining - available : 0u;
        data += csvTextCell(row.itemId.empty() ? row.typeName : row.itemId) + "," + "0" + "," + csvTextCell(label(index));
        for (auto n : {saturatingScale(counts.total, multiplier),
                       saturatingScale(counts.missing, multiplier),
                       std::uint64_t{0},
                       std::uint64_t{0},
                       std::uint64_t{0},
                       available,
                       toGather})
            data += "," + std::to_string(n);
        data += "\r\n";
    }
    return data;
}

// Emits the upstream JSON member set and ordering:
//   [{"item": ..., "aux": ..., "label": ..., "total": ..., "missing": ..., "available": ...}]
[[nodiscard]] inline std::string renderJeJson(
    detail::MaterialListSnapshot const& snapshot,
    JeExportOptions const& options = {}
) {
    auto const multiplier = options.multiplier == 0 ? 1u : options.multiplier;
    auto label = [&](std::size_t index) -> std::string const& {
        if (options.names && index < options.names->size() && !(*options.names)[index].empty())
            return (*options.names)[index];
        return snapshot.requirements[index].displayName;
    };
    std::vector<std::size_t> included;
    for (std::size_t index = 0; index < snapshot.requirements.size(); ++index) {
        if (!snapshot.matches(index, options.filter)) continue;
        if (options.filter != detail::MaterialFilter::Ignored && snapshot.isIgnored(index)) continue;
        included.push_back(index);
    }
    std::string data = "[\n";
    for (std::size_t position = 0; position < included.size(); ++position) {
        auto const index = included[position];
        auto const& row  = snapshot.requirements[index];
        auto const missing = snapshot.shortage(index).value_or(row.count);
        std::uint64_t const available = index < snapshot.available.size() && snapshot.available[index]
            ? static_cast<std::uint64_t>(std::max(0, *snapshot.available[index]))
            : 0u;
        data += "  {\"item\": " + jsonQuote(row.itemId.empty() ? row.typeName : row.itemId)
              + ", \"aux\": 0, \"label\": " + jsonQuote(label(index))
              + ", \"total\": " + std::to_string(saturatingScale(row.count, multiplier))
              + ", \"missing\": " + std::to_string(saturatingScale(missing, multiplier))
              + ", \"available\": " + std::to_string(available) + "}"
              + (position + 1 < included.size() ? ",\n" : "\n");
    }
    data += "]\n";
    return data;
}

} // namespace lholo::structure::material_rules
