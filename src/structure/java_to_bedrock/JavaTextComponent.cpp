// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "structure/java_to_bedrock/JavaBlockEntityToBedrock.h"

#include <nlohmann/json.hpp>
#include <vector>

namespace lholo::structure::detail {
namespace {

void appendJavaText(nlohmann::json const& value, std::string& output) {
    // A legal 65535-byte NBT string can contain tens of thousands of nested
    // JSON arrays. Preserve traversal order using owned heap storage instead
    // of consuming the native callback's C++ stack once per component.
    std::vector<nlohmann::json const*> pending{&value};
    while (!pending.empty()) {
        auto const& current = *pending.back();
        pending.pop_back();
        if (current.is_string()) {
            output += current.get_ref<std::string const&>();
            continue;
        }
        if (current.is_array()) {
            for (auto child = current.rbegin(); child != current.rend(); ++child) pending.push_back(&*child);
            continue;
        }
        if (!current.is_object()) continue;

        if (auto const text = current.find("text"); text != current.end() && text->is_string()) {
            output += text->get_ref<std::string const&>();
        } else if (auto const fallback = current.find("fallback");
                   fallback != current.end() && fallback->is_string()) {
            output += fallback->get_ref<std::string const&>();
        } else if (auto const translate = current.find("translate");
                   translate != current.end() && translate->is_string()) {
            // Client language tables are not available in this converter.
            output += translate->get_ref<std::string const&>();
        }
        if (auto const extra = current.find("extra"); extra != current.end()) pending.push_back(&*extra);
    }
}

} // namespace

std::string javaTextComponentToPlainText(std::string const& component) {
    if (component.empty()) return {};
    auto const parsed = nlohmann::json::parse(component, nullptr, false, true);
    if (parsed.is_discarded()) return component;
    std::string result;
    appendJavaText(parsed, result);
    return result;
}

} // namespace lholo::structure::detail
