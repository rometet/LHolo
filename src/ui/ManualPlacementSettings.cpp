// LHolo - Manual placement preferences panel
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ManualPlacementSettings.h"

#include "ui/MenuWidgets.h"
#include "i18n/Translator.h"
#include "place/ManualPlacementPolicy.h"

#include <array>
#include <cstdio>
#include <string>

namespace lholo::ui {
namespace {

// Display-boundary text: the Japanese fork and English fallback share one
// editor. No translated labels or language indices are stored in preferences.
char const* label(char const* japanese, char const* english) {
    return i18n::languageCode(i18n::language()) == "ja_JP" ? japanese : english;
}

std::array<char, place::kManualAllowlistMaxTextLength + 1> gEditor{};
bool gEditorInitialized{};
bool gEditorDirty{};
bool gSaved{};
std::string gLoadedText;

void loadEditor(place::ManualPlacementPolicySnapshot const& snapshot) {
    auto const text = place::manualAllowedItemsText(snapshot.items);
    std::snprintf(gEditor.data(), gEditor.size(), "%s", text.c_str());
    gEditorInitialized = true;
    gEditorDirty = false;
    gLoadedText = text;
}

} // namespace

void renderManualPlacementSettings(UiMetrics const& metrics) {
    auto snapshot = place::manualPlacementPolicySnapshot();
    if (!gEditorInitialized || (!gEditorDirty
        && place::manualAllowedItemsText(snapshot.items) != gLoadedText)) {
        loadEditor(snapshot);
        gSaved = false;
    }
    renderSection(
        "##ManualPlacementAllowlist",
        label("手動設置：制限しないブロック", "Manual placement: unrestricted blocks"),
        metrics,
        [&] {
            ImGui::TextWrapped("%s", label(
                "登録したブロックをメインハンドに持っている間は、投影の外でも通常どおり設置できます。投影への自動持ち替えも行いません。空欄なら従来の制限を維持します。",
                "While holding a listed block in your main hand, vanilla placement works even outside the projection. LHolo will not auto-switch materials. Leave empty to keep all restrictions."
            ));
            ImGui::TextWrapped("%s", label(
                "設置に使うアイテムIDを1行ずつ、またはカンマ区切りで入力してください。例：minecraft:dirt / minecraft:scaffolding。minecraft: は省略できます。",
                "Enter placement item IDs, one per line or comma-separated. Examples: minecraft:dirt, minecraft:scaffolding. The minecraft: namespace is optional."
            ));
            if (ImGui::InputTextMultiline(
                    "##ManualAllowedBlockIds", gEditor.data(), gEditor.size(),
                    ImVec2(-1.0f, 110.0f * metrics.scale)
                )) {
                gSaved = false;
                gEditorDirty = true;
            }

            auto const parsed = place::parseManualAllowedItems(gEditor.data());
            if (!parsed.valid()) {
                ImGui::TextWrapped("%s", label(
                    "IDの書式または件数が不正です。ワイルドカードは使えません。上限は256件です。",
                    "Invalid ID or too many entries. Wildcards are not allowed; the limit is 256 entries."
                ));
            }
            ImGui::BeginDisabled(!parsed.valid());
            if (ImGui::Button(label("保存して適用", "Save and apply"))) {
                gSaved = place::saveManualPlacementPolicy(gEditor.data()) == place::ManualPlacementPolicyError::None;
                snapshot = place::manualPlacementPolicySnapshot();
                if (gSaved) loadEditor(snapshot);
            }
            ImGui::EndDisabled();
            if (!metrics.compact) ImGui::SameLine();
            if (ImGui::Button(label("ファイルから再読込", "Reload from file"))) {
                gSaved = false;
                if (place::reloadManualPlacementPolicy() == place::ManualPlacementPolicyError::None) {
                    snapshot = place::manualPlacementPolicySnapshot();
                    loadEditor(snapshot);
                }
            }
            if (!metrics.compact) ImGui::SameLine();
            if (ImGui::Button(label("入力をクリア", "Clear editor"))) {
                gEditor.fill('\0'); // Does not change the active list until Save.
                gEditorDirty = true;
                gSaved = false;
            }
            snapshot = place::manualPlacementPolicySnapshot();
            ImGui::TextDisabled(label("適用中：%zu件（このクライアントの設定）", "Active: %zu entries (this client only)"), snapshot.items.size());
            if (snapshot.error != place::ManualPlacementPolicyError::None) {
                ImGui::TextWrapped("%s", label(
                    "設定の読込・保存に失敗しました。最後に正常適用したリストを維持しています。ファイルの内容と書込権限を確認してください。",
                    "Loading/saving failed. The last valid list is unchanged. Check the file contents and write permissions."
                ));
            } else if (gSaved) {
                ImGui::TextDisabled("%s", label("保存しました。次の操作から適用されます。", "Saved. Applies to the next input."));
            }
            ImGui::TextWrapped("%s", label(
                "保存先：mods/LHolo/config/manual-placement-allowlist.txt。サーバーの設置権限や通常の設置条件は変更しません。",
                "Saved to mods/LHolo/config/manual-placement-allowlist.txt. Server permissions and vanilla placement requirements still apply."
            ));
        }
    );
}

} // namespace lholo::ui
