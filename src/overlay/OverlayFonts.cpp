// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi

#include "overlay/OverlayFonts.h"

#include <imgui.h>

namespace lholo::overlay {

void loadOverlayFonts(ImFontAtlas& atlas, OverlayFontFiles const& files) {
    ImFontConfig config{};
    config.OversampleH = 2;
    config.OversampleV = 2;
    auto const chineseFont = files.chinese.string();
    if (std::filesystem::exists(chineseFont)) {
        // Build the atlas at 2x the logical base size. A 4K/default 2x UI can
        // then render at native font resolution instead of magnifying an 18px
        // atlas, which made text and navigation edges look pixelated.
        atlas.AddFontFromFileTTF(
            chineseFont.c_str(),
            36.0f,
            &config,
            atlas.GetGlyphRangesChineseFull()
        );

        // Merge Cyrillic glyphs into the already loaded font. Use ImGui's
        // stable built-in range pointer; atlas construction happens later,
        // so a temporary range buffer must not be passed here.
        ImFontConfig cyrillicConfig = config;
        cyrillicConfig.MergeMode = true;
        atlas.AddFontFromFileTTF(
            chineseFont.c_str(),
            36.0f,
            &cyrillicConfig,
            atlas.GetGlyphRangesCyrillic()
        );
    } else {
        // Theme scaling assumes a 36px atlas regardless of which optional
        // Windows fonts are installed. ImGui's unconfigured fallback is 13px.
        config.SizePixels = 36.f;
        atlas.AddFontDefault(&config);
    }

    // Merge Japanese glyphs missing from the base atlas. ImGui retains the
    // base font's glyph when both sources contain the same code point.
    auto const japaneseFont = files.japanese.string();
    if (std::filesystem::exists(japaneseFont)) {
        ImFontConfig japaneseConfig = config;
        japaneseConfig.MergeMode = true;
        atlas.AddFontFromFileTTF(
            japaneseFont.c_str(),
            36.0f,
            &japaneseConfig,
            atlas.GetGlyphRangesJapanese()
        );
    }

    // Chinese glyph ranges do not include the warning sign used by the
    // experimental-feature notice. Merge that single glyph from Windows'
    // symbol font so the original label is rendered instead of as '?'.
    auto const symbolFont = files.symbols.string();
    if (std::filesystem::exists(symbolFont)) {
        static constexpr ImWchar warningGlyphRange[]{0x26A0, 0x26A0, 0};
        ImFontConfig symbolConfig{};
        symbolConfig.MergeMode = true;
        symbolConfig.PixelSnapH = true;
        symbolConfig.OversampleH = 2;
        symbolConfig.OversampleV = 2;
        atlas.AddFontFromFileTTF(symbolFont.c_str(), 36.0f, &symbolConfig, warningGlyphRange);
    }
}

} // namespace lholo::overlay
