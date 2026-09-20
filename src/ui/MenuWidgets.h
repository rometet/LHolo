// LHolo - Fluent-style menu widgets

#pragma once

#include "ui/LHoloMenu.h"

#include <imgui.h>

namespace lholo::ui {

float fieldWidth(UiMetrics const& metrics);
float numericFieldWidth(UiMetrics const& metrics);
float adaptiveComboWidth(char const* const* items, int count);

// JE-style stack count: below one stack -> "N"; otherwise "N (a x S + b)" (the
// "+ b" omitted when it divides evenly), e.g. 111 -> "111 (1 x 64 + 47)",
// 18 buckets (S=1) -> "18 (18 x 1)".
std::string formatStackCount(std::uint64_t count, int stackSize);

// Material names come either from the game (displayName) or, for projected
// liquids that have no in-game item to take a name from, from the interface
// language table (nameKey). Every material row must resolve its name here so
// the menu and the HUD never disagree.
char const* materialDisplayName(
    std::string const&                  displayName,
    std::optional<i18n::TextKey> const& nameKey
);

template <typename Body>
void renderSection(char const* id, char const* title, UiMetrics const& metrics, Body&& body) {
    // A single page owns scrolling; each section auto-sizes vertically and
    // acts only as a visual card, so nested scrollbars never appear.
    auto cardColor = ImGui::GetStyleColorVec4(ImGuiCol_TableRowBgAlt);
    cardColor.w = 0.96f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, cardColor);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(metrics.sectionPadding, metrics.sectionPadding));
    if (ImGui::BeginChild(
            id,
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
        )) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.35f));
        body();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.45f));
}

template <typename Control>
void renderValueRow(char const* label, UiMetrics const& metrics, Control&& control) {
    if (metrics.compact) {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        control();
        return;
    }
    ImGui::SetNextItemWidth(fieldWidth(metrics));
    control();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

template <typename Control>
void renderNumericValueRow(char const* label, UiMetrics const& metrics, Control&& control) {
    if (metrics.compact) {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        control();
        return;
    }
    ImGui::SetNextItemWidth(numericFieldWidth(metrics));
    control();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

void renderCheckboxRow(char const* id, char const* label, bool& value, UiMetrics const& metrics);
void drawCenteredInputValue(char const* text, ImVec2 minimum, ImVec2 maximum);

void renderSteppedInt(
    char const*    id,
    char const*    label,
    int&           value,
    int            minimum,
    int            maximum,
    UiMetrics const& metrics
);

void renderSteppedFloat(
    char const*    id,
    char const*    label,
    float&         value,
    float          minimum,
    float          maximum,
    float          step,
    UiMetrics const& metrics
);

} // namespace lholo::ui
