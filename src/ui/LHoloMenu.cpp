// LHolo - Fluent-style menu

#include "ui/LHoloMenu.h"

#include "ui/MenuWidgets.h"

#include "ui/MenuPages.h"
#include "ui/ManualPlacementSettings.h"

#include <algorithm>

namespace lholo::ui {
namespace {
void renderTarget(MenuModel const& model) {
    auto const* placement = structure::selectedPlacement(model.schematic.session.document);
    auto const label = std::string{i18n::tr(i18n::TextKey::MenuTarget)} + ": "
        + (placement ? placement->name : i18n::tr(i18n::TextKey::MenuNoTarget));
    auto visible = label;
    auto const width = ImGui::GetContentRegionAvail().x;
    if (ImGui::CalcTextSize(visible.c_str()).x > width) {
        while (!visible.empty() && ImGui::CalcTextSize((visible + "...").c_str()).x > width) {
            auto at = visible.size() - 1;
            while (at && (static_cast<unsigned char>(visible[at]) & 0xC0) == 0x80) --at;
            visible.erase(at);
        }
        visible += "...";
    }
    ImGui::TextDisabled("%s", visible.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(label.c_str());
        if (placement) ImGui::TextUnformatted(placement->file.c_str());
        ImGui::EndTooltip();
    }
}
} // namespace

void renderMenu(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    bool open = true;
    auto const display = ImGui::GetIO().DisplaySize;
    auto const margin = std::max(18.0f * metrics.scale, std::min(display.x, display.y) * 0.025f);
    auto const windowSize = ImVec2(
        std::min(std::max(display.x * 0.86f, 720.0f), std::max(320.0f, display.x - margin * 2.0f)),
        std::min(std::max(display.y * 0.84f, 480.0f), std::max(280.0f, display.y - margin * 2.0f))
    );
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    if (ImGui::Begin(
            "##LHoloMenu", &open,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavFocus
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
        )) {
        if (!metrics.compact) {
            auto const available = ImGui::GetContentRegionAvail();
            auto navText = 0.0f;
            for (std::size_t page = 0; page < kMenuPageCount; ++page) {
                navText = std::max(
                    navText,
                    ImGui::CalcTextSize(pageName(static_cast<MenuPage>(page))).x
                );
            }
            auto navWidth = std::max(navText + metrics.outerPadding * 2.2f, available.x * 0.22f);
            navWidth = std::min(navWidth, available.x * 0.36f);
            if (ImGui::BeginChild(
                    "##Navigation", ImVec2(navWidth, 0.0f), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar
                )) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
                ImGui::TextUnformatted("LHolo");
                ImGui::PopStyleColor();
                ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s",i18n::tr(i18n::TextKey::MenuTagline));
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.35f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.35f));
                renderNavigation(model, metrics);
            }
            ImGui::EndChild();
            ImGui::SameLine(0.0f, metrics.gap);
        }
        if (ImGui::BeginChild(
                "##MainPanel", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
            )) {
            auto const closeLabel = i18n::tr(metrics.compact ? i18n::TextKey::ButtonClose : i18n::TextKey::MenuClose);
            auto const closeWidth = ImGui::CalcTextSize(closeLabel).x
                + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (metrics.compact) {
                ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - closeWidth - metrics.gap));
                renderNavigation(model, metrics, true);
            } else {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(pageName(model.page));
            }
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(
                ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - closeWidth
            ));
            if (ImGui::Button(closeLabel)) open = false;
            renderTarget(model);
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, metrics.gap * 0.25f));

            bool renderBody = true;
            if (model.page == MenuPage::Verification) {
                if (ImGui::BeginChild("##VerificationToolbar", ImVec2(0, 0),
                    ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                    renderBody = renderVerificationToolbar(model, actions, metrics);
                }
                ImGui::EndChild();
            }
            if (ImGui::BeginChild(
                    "##PageScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoSavedSettings
                )) {
                switch (model.page) {
                case MenuPage::Projection: renderProjectionPage(model, actions, metrics); break;
                case MenuPage::Schematics: renderSchematicsPage(model, actions, metrics); break;
                case MenuPage::Verification:
                    if (renderBody) renderVerificationPage(model, actions, metrics, false);
                    break;
                case MenuPage::Materials: renderMaterialsPage(model, actions, metrics); break;
                case MenuPage::CreateStructure: renderCreateStructurePage(model, actions, metrics); break;
                case MenuPage::Experimental:
                    renderExperimentalPage(model, actions, metrics);
                    renderManualPlacementSettings(model, metrics);
                    break;
                case MenuPage::Transform: renderTransformPage(model, metrics); break;
                case MenuPage::Render: renderRenderPage(model, actions, metrics); break;
                case MenuPage::Hotkeys: renderHotkeysPage(model, actions, metrics); break;
                case MenuPage::Hud: renderHudPage(model, metrics); break;
                case MenuPage::Interface: renderInterfacePage(model, metrics); break;
                case MenuPage::Count: break;
                }
                if (model.materialPopupRequested) {
                    auto const popupName = materialPopupName();
                    ImGui::OpenPopup(popupName.c_str());
                }
                // Both opening and rendering happen in this page child. Dear
                // ImGui popup IDs are scoped to the current window.
                renderMaterialPopup(model, metrics);
                if(model.directMenuRoute!=input::MenuRoute::None)ImGui::SetScrollY(0);
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }
    ImGui::End();
    if (!open) model.closeRequested = true;
    model.directMenuRoute=input::MenuRoute::None;
}

} // namespace lholo::ui
