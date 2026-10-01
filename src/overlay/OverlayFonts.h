// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi

#pragma once

#include <filesystem>

struct ImFontAtlas;

namespace lholo::overlay {

struct OverlayFontFiles {
    std::filesystem::path chinese;
    std::filesystem::path japanese;
    std::filesystem::path symbols;
};

// The atlas borrows only ImGui's built-in and static glyph ranges. Font data
// loaded from these optional files is owned by the atlas until it is cleared.
void loadOverlayFonts(ImFontAtlas& atlas, OverlayFontFiles const& files);

} // namespace lholo::overlay
