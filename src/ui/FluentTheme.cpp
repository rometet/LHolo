// LHolo - Fluent-style Dear ImGui theme

#include "ui/FluentTheme.h"

#include <algorithm>
#include <cmath>

namespace lholo::ui {
namespace {

bool       gBaseStyleReady{};
ImGuiStyle gBaseStyle{};
float      gLastScale{-1.0f};
ImVec2     gLastViewport{-1.0f, -1.0f};

// The 36px CJK atlas is displayed at a comfortable density while uiScale
// remains the single user-facing size control. Keeping this modest leaves
// enough room for the centered panel layout at 1080p.
constexpr float kMenuDensity{1.18f};

ImVec4 color(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }

} // namespace

UiMetrics calculateMetrics(ImVec2 viewport, float uiScale) {
    UiMetrics metrics;
    metrics.viewport = viewport;
    metrics.scale = std::clamp(uiScale, 1.0f, 5.0f);
    auto const logicalWidth = viewport.x / metrics.scale;
    metrics.compact = logicalWidth < 900.0f || viewport.x < viewport.y * 1.15f;
    // These are logical dimensions and are scaled once below, rather than
    // being tied to a particular resolution.
    metrics.gap = 8.0f * metrics.scale * kMenuDensity;
    metrics.outerPadding = 16.0f * metrics.scale * kMenuDensity;
    metrics.sectionPadding = 12.0f * metrics.scale * kMenuDensity;
    metrics.rounding = 8.0f * metrics.scale * kMenuDensity;
    return metrics;
}

void applyFluentTheme(UiMetrics const& metrics) {
    if (!gBaseStyleReady) {
        gBaseStyle = ImGui::GetStyle();
        gBaseStyleReady = true;
    }
    auto const viewportChanged = std::abs(gLastViewport.x - metrics.viewport.x) > 0.5f
        || std::abs(gLastViewport.y - metrics.viewport.y) > 0.5f;
    if (std::abs(gLastScale - metrics.scale) < 0.001f && !viewportChanged) return;

    auto style = gBaseStyle;
    style.ScaleAllSizes(metrics.scale * kMenuDensity);
    style.WindowPadding = {metrics.outerPadding, metrics.outerPadding};
    style.FramePadding = {metrics.sectionPadding * 0.82f, metrics.sectionPadding * 0.42f};
    style.ItemSpacing = {metrics.gap, metrics.gap};
    style.ItemInnerSpacing = {metrics.gap * 0.7f, metrics.gap * 0.6f};
    style.WindowRounding = metrics.rounding * 1.25f;
    style.ChildRounding = metrics.rounding;
    style.FrameRounding = metrics.rounding * 0.58f;
    style.PopupRounding = metrics.rounding;
    style.ScrollbarRounding = metrics.rounding;
    style.GrabRounding = metrics.rounding;
    style.WindowBorderSize = 1.0f * metrics.scale;
    style.ChildBorderSize = 1.0f * metrics.scale;
    style.FrameBorderSize = 1.0f * metrics.scale;
    style.PopupBorderSize = 1.0f * metrics.scale;

    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = color(0.95f, 0.95f, 0.98f);
    colors[ImGuiCol_TextDisabled] = color(0.56f, 0.57f, 0.66f);
    colors[ImGuiCol_WindowBg] = color(0.045f, 0.047f, 0.070f, 0.985f);
    colors[ImGuiCol_ChildBg] = color(0.065f, 0.067f, 0.098f, 0.98f);
    colors[ImGuiCol_PopupBg] = color(0.070f, 0.072f, 0.108f, 0.99f);
    colors[ImGuiCol_TitleBg] = color(0.070f, 0.072f, 0.108f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = color(0.090f, 0.085f, 0.145f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = color(0.055f, 0.057f, 0.082f, 1.0f);
    colors[ImGuiCol_Border] = color(0.255f, 0.225f, 0.480f, 0.78f);
    colors[ImGuiCol_FrameBg] = color(0.105f, 0.105f, 0.155f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = color(0.155f, 0.145f, 0.245f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = color(0.205f, 0.185f, 0.335f, 1.0f);
    colors[ImGuiCol_Button] = color(0.125f, 0.115f, 0.205f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = color(0.285f, 0.245f, 0.555f, 1.0f);
    colors[ImGuiCol_ButtonActive] = color(0.405f, 0.340f, 0.820f, 1.0f);
    colors[ImGuiCol_Header] = color(0.205f, 0.180f, 0.385f, 0.92f);
    colors[ImGuiCol_HeaderHovered] = color(0.310f, 0.265f, 0.600f, 1.0f);
    colors[ImGuiCol_HeaderActive] = color(0.405f, 0.340f, 0.820f, 1.0f);
    colors[ImGuiCol_CheckMark] = color(0.620f, 0.515f, 1.0f, 1.0f);
    colors[ImGuiCol_SliderGrab] = color(0.555f, 0.455f, 0.940f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = color(0.720f, 0.630f, 1.0f, 1.0f);
    colors[ImGuiCol_Separator] = color(0.235f, 0.205f, 0.420f, 0.82f);
    colors[ImGuiCol_TableHeaderBg] = color(0.130f, 0.120f, 0.210f, 1.0f);
    colors[ImGuiCol_TableBorderStrong] = color(0.285f, 0.245f, 0.510f, 1.0f);
    colors[ImGuiCol_TableBorderLight] = color(0.185f, 0.170f, 0.310f, 1.0f);
    colors[ImGuiCol_TableRowBg] = color(0.085f, 0.085f, 0.125f, 0.72f);
    colors[ImGuiCol_TableRowBgAlt] = color(0.115f, 0.105f, 0.175f, 0.82f);
    colors[ImGuiCol_ModalWindowDimBg] = color(0.0f, 0.0f, 0.0f, 0.62f);

    ImGui::GetStyle() = style;
    // The existing atlas is 36px. Rendering it at half scale gives a crisp
    // 18px logical default while keeping headroom for 4K automatic scaling.
    ImGui::GetIO().FontGlobalScale = metrics.scale * 0.5f * kMenuDensity;
    gLastScale = metrics.scale;
    gLastViewport = metrics.viewport;
}

void resetFluentTheme() {
    if (ImGui::GetCurrentContext()) ImGui::GetIO().FontGlobalScale = 1.0f;
    gBaseStyleReady = false;
    gLastScale = -1.0f;
    gLastViewport = {-1.0f, -1.0f};
}

} // namespace lholo::ui
