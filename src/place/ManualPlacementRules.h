// LHolo - Manual placement allow-list rules (no game or renderer dependencies)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace lholo::place {

inline constexpr std::size_t kManualAllowlistMaxItems = 256;
inline constexpr std::size_t kManualAllowlistMaxIdLength = 128;
inline constexpr std::size_t kManualAllowlistMaxTextLength = 65536;

enum class ManualAllowlistParseError { None, InvalidId, TooManyItems, TooMuchText };

struct ManualAllowlistParseResult {
    std::vector<std::string> items;
    ManualAllowlistParseError error{ManualAllowlistParseError::None};
    [[nodiscard]] bool valid() const noexcept { return error == ManualAllowlistParseError::None; }
};

inline bool manualAllowlistWhitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Accept a fully qualified resource id, or add the vanilla namespace. Do not
// accept wildcards, tags, NBT/state suffixes or partial/prefix matches.
inline std::string normalizeManualAllowedItem(std::string_view input) {
    while (!input.empty() && manualAllowlistWhitespace(input.front())) input.remove_prefix(1);
    while (!input.empty() && manualAllowlistWhitespace(input.back())) input.remove_suffix(1);
    if (input.empty() || input.size() > kManualAllowlistMaxIdLength) return {};
    std::string id{input};
    for (char& c : id) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (id.find(':') == std::string::npos) id.insert(0, "minecraft:");
    if (id.size() > kManualAllowlistMaxIdLength) return {};
    auto const colon = id.find(':');
    if (colon == 0 || colon + 1 == id.size()) return {};
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i == colon) continue;
        char const c = id[i];
        bool const common = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '_' || c == '-' || c == '.';
        if (!common && !(i > colon && c == '/')) return {};
    }
    return id;
}

inline ManualAllowlistParseResult parseManualAllowedItems(std::string_view text) {
    ManualAllowlistParseResult result;
    if (text.size() > kManualAllowlistMaxTextLength) {
        result.error = ManualAllowlistParseError::TooMuchText;
        return result;
    }
    // UTF-8 BOM from Windows text editors is allowed only at file start.
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    while (!text.empty()) {
        auto const end = text.find_first_of(",;\n\r");
        auto token = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        while (!token.empty() && manualAllowlistWhitespace(token.front())) token.remove_prefix(1);
        while (!token.empty() && manualAllowlistWhitespace(token.back())) token.remove_suffix(1);
        if (token.empty()) continue;
        auto id = normalizeManualAllowedItem(token);
        if (id.empty()) {
            result.items.clear();
            result.error = ManualAllowlistParseError::InvalidId;
            return result;
        }
        result.items.push_back(std::move(id));
        if (result.items.size() > kManualAllowlistMaxItems) {
            result.items.clear();
            result.error = ManualAllowlistParseError::TooManyItems;
            return result;
        }
    }
    std::sort(result.items.begin(), result.items.end());
    result.items.erase(std::unique(result.items.begin(), result.items.end()), result.items.end());
    return result;
}

// The parser publishes sorted, normalized ids. Runtime item ids are already
// qualified; deliberately do not normalize/allocate on the input-hook path.
inline bool manualAllowedItemContains(std::span<std::string const> items, std::string_view id) {
    if (id.empty()) return false;
    return std::binary_search(items.begin(), items.end(), id, [](auto const& lhs, auto const& rhs) {
        return std::string_view{lhs} < std::string_view{rhs};
    });
}

inline std::string manualAllowedItemsText(std::span<std::string const> items) {
    std::string text;
    for (auto const& id : items) {
        text += id;
        text += '\n';
    }
    return text;
}

} // namespace lholo::place
