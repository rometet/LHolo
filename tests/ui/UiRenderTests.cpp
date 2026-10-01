#include "ui/LHoloMenu.h"
#include "ui/MenuPages.h"
#include "ui/FileDialog.h"
#include "i18n/LanguageStore.h"
#include "overlay/ImGuiFrameRecovery.h"
#include "overlay/OverlayFonts.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
std::size_t frames{};
std::size_t checks{};
std::size_t errors{};

void check(bool passed, char const* message) {
    ++checks;
    if (!passed) throw std::runtime_error(message);
}

void errorCallback(ImGuiContext*, void*, char const* message) {
    ++errors;
    std::fprintf(stderr, "ImGui error: %s\n", message);
}

void testMissingOptionalFonts() {
    ImFontAtlas atlas;
    lholo::overlay::loadOverlayFonts(atlas, {});
    check(atlas.Fonts.Size == 1, "fallback has one base font");
    check(atlas.Build(), "fallback atlas builds without Windows fonts");
    auto* font = atlas.Fonts[0];
    std::printf("Missing optional fonts: base size=%.1fpx\n", font->FontSize);
    check(font->FontSize == 36.f, "fallback preserves the 36px UI base size");
    auto const* glyph = font->FindGlyphNoFallback('A');
    check(glyph && glyph->AdvanceX > 0 && glyph->Y1 > glyph->Y0, "fallback contains readable Latin glyphs");
}

void renderPages(ImVec2 viewport, float scale, int state, int language) {
    auto* context = ImGui::CreateContext();
    context->ErrorCallback = errorCallback;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = viewport;
    io.DeltaTime = 1.f / 60.f;
    io.ConfigErrorRecoveryEnableAssert = false;
    ImFontConfig font;
    font.SizePixels = 36.f;
    io.Fonts->AddFontDefault(&font);
    unsigned char* pixels{};
    int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    check(pixels && width > 0 && height > 0, "atlas creation");

    std::array<char, lholo::ui::StructurePathUtf8Capacity> path{};
    std::snprintf(path.data(), path.size(), "%s", "C:/schematics/test.litematic");
    lholo::ui::MenuModel model;
    model.pathBuffer = path.data();
    model.pathBufferSize = path.size();
    model.uiScale = scale;
    model.language = language;
    model.status = "Status with wrapping text and %% formatting markers";
    model.captureStatus = "Capture status";
    model.captureRevision = frames + 1;
    model.hasLoadedStructure = state != 0;
    model.hasSavedProjection = state != 0;
    model.captureWorldAvailable = state != 0;
    model.experimentalConsent = state == 2;
    model.blockOpeningInput = state == 0;
    model.manualPlace = state == 2;
    model.hudEnabled = state != 0;
    model.materialHudEnabled = state == 2;
    model.maxLayerX = 63;
    model.maxLayerY = 255;
    model.materialCount = 32;
    model.capture.first = {state != 0, -17, -64, -1};
    model.capture.second = {state != 0, 16, 319, 16};
    model.manualPlacementAllowedItems = {"minecraft:scaffolding", "minecraft:stone"};
    model.materials = {
        {"A long material name which can wrap at narrow viewport sizes", {}, "minecraft:stone", 111, 64},
        {"Water", lholo::i18n::TextKey::MaterialWater, "minecraft:water", 64, 1},
        {"Sign", {}, "minecraft:oak_sign", std::numeric_limits<std::uint64_t>::max(), 16}
    };
    for (std::size_t index = 0; index < model.hotkeys.size(); ++index) {
        model.hotkeys[index] = {static_cast<lholo::ui::HotkeyId>(index),
            "Hotkey " + std::to_string(index), "Ctrl + F10", index == 0 && state == 2};
    }
    auto const metrics = lholo::ui::calculateMetrics(viewport, scale);
    lholo::ui::applyFluentTheme(metrics);
    lholo::ui::MenuActions const actions{};
    for (std::size_t page = 0; page < lholo::ui::kMenuPageCount; ++page) {
        model.page = static_cast<lholo::ui::MenuPage>(page);
        model.layerAxis = static_cast<int>(page % 3);
        model.layerDisplayMode = static_cast<int>(page % 4);
        for (int repeat = 0; repeat < 3; ++repeat) {
            model.materialPopupRequested = state == 2 && repeat == 0;
            ImGui::NewFrame();
            {
                lholo::overlay::detail::ImGuiFrameRecovery recovery;
                lholo::ui::renderMenu(model, actions, metrics);
                check(context->CurrentWindowStack.Size == 1, "menu window stack");
                check(context->ColorStack.empty() && context->StyleVarStack.empty()
                    && context->FontStack.empty() && context->FocusScopeStack.Size == 1
                    && context->GroupStack.empty() && context->ItemFlagsStack.Size == 1
                    && context->BeginPopupStack.empty() && context->DisabledStackSize == 0
                    && context->CurrentTable == nullptr, "menu scopes balanced");
                ImGui::Render();
            }
            auto const* data = ImGui::GetDrawData();
            check(data && data->Valid && data->TotalVtxCount > 0, "menu emitted draw data");
            check(!context->WithinFrameScope && context->CurrentWindowStack.empty(), "menu frame ended");
            check(context->ErrorCountCurrentFrame == 0 && errors == 0, "menu ImGui diagnostics");
            bool finite = true;
            for (auto const* list : data->CmdLists) {
                for (auto const& vertex : list->VtxBuffer) {
                    finite = finite && std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y)
                        && std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y);
                }
                for (auto const& command : list->CmdBuffer) {
                    finite = finite && std::isfinite(command.ClipRect.x) && std::isfinite(command.ClipRect.y)
                        && std::isfinite(command.ClipRect.z) && std::isfinite(command.ClipRect.w);
                }
            }
            check(finite, "finite menu geometry");
            ++frames;
        }
    }
    lholo::ui::resetFluentTheme();
    ImGui::DestroyContext(context);
}
}

int main() {
    try {
        testMissingOptionalFonts();
        lholo::i18n::initLanguageStore();
        for (std::size_t language = 0; language < lholo::i18n::languages().size(); ++language) {
            check(lholo::i18n::setLanguageByCode(lholo::i18n::languageCode(language)), "language selection");
            for (auto const viewport : {ImVec2{1920, 1080}, ImVec2{3840, 2160},
                    ImVec2{640, 480}, ImVec2{480, 800}}) {
                for (float scale : {1.f, 2.f, 5.f}) {
                    for (int state = 0; state < 3; ++state) renderPages(viewport, scale, state, static_cast<int>(language));
                }
            }
        }
        std::printf("UI render checks=%zu frames=%zu ImGui_errors=%zu\n", checks, frames, errors);
        return 0;
    } catch (std::exception const& exception) {
        std::fprintf(stderr, "UI render failed after %zu frames: %s\n", frames, exception.what());
        return 1;
    }
}
