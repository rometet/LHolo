// LHolo - Per-client manual placement exception editor
// Copyright (C) 2026  MarmieQi and contributors
#include "ui/ManualPlacementSettings.h"
#include "ui/MenuWidgets.h"
#include "place/ManualPlacementRules.h"

#include <algorithm>
#include <array>
#include <imgui.h>

namespace lholo::ui {
void renderManualPlacementSettings(MenuModel& model, UiMetrics const& metrics) {
    renderSection("##ManualPlacementAllowedItems",
        i18n::tr(i18n::TextKey::SectionManualPlacementAllowedItems), metrics, [&] {
        static std::array<char, place::detail::kMaxManualPlacementItemIdLength + 1> input{};
        static bool invalid{};
        ImGui::TextWrapped("%s", i18n::tr(i18n::TextKey::HintManualPlacementAllowedItems));
        ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x));
        bool const enter = ImGui::InputTextWithHint(
            "##ManualPlacementItemId", "minecraft:scaffolding", input.data(), input.size(),
            model.blockOpeningInput ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_EnterReturnsTrue
        );
        if (ImGui::Button(i18n::tr(i18n::TextKey::ButtonAddAllowedItem)) || enter) {
            auto normalized = place::detail::normalizeManualPlacementItemId(input.data());
            invalid = !normalized;
            if (normalized) {
                auto& items = model.manualPlacementAllowedItems;
                if (std::find(items.begin(), items.end(), *normalized) != items.end()) {
                    input.fill('\0');
                } else if (items.size() < place::detail::kMaxManualPlacementAllowedItems) {
                    items.push_back(std::move(*normalized));
                    std::sort(items.begin(), items.end());
                    input.fill('\0');
                } else {
                    invalid = true;
                }
            }
        }
        if (invalid) ImGui::TextWrapped("%s", i18n::tr(i18n::TextKey::HintInvalidAllowedItem));
        if (model.manualPlacementAllowedItems.empty()) {
            ImGui::TextDisabled("%s", i18n::tr(i18n::TextKey::HintNoAllowedItems));
        }
        auto& items = model.manualPlacementAllowedItems;
        for (std::size_t index = 0; index < items.size();) {
            ImGui::PushID(items[index].c_str());
            bool const remove = ImGui::SmallButton(i18n::tr(i18n::TextKey::ButtonRemoveAllowedItem));
            ImGui::SameLine();
            ImGui::TextUnformatted(items[index].c_str());
            ImGui::PopID();
            if (remove) items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
            else ++index;
        }
    });
}
} // namespace lholo::ui
