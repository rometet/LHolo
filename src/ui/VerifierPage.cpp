#include "ui/MenuPages.h"
#include "ui/PresentationText.h"
#include "ui/MenuWidgets.h"
#include <cmath>
#include <cstdio>

namespace lholo::ui {
namespace {
using namespace structure;
using i18n::TextKey;
constexpr std::array kinds{VerificationState::Missing, VerificationState::WrongType,
    VerificationState::WrongState, VerificationState::Extra};
constexpr std::array categoryKeys{TextKey::VerifierMissing, TextKey::VerifierWrongType,
    TextKey::VerifierWrongState, TextKey::VerifierExtra};
constexpr std::array categoryColors{ImVec4{1.f,.40f,.44f,1.f}, ImVec4{1.f,.65f,.32f,1.f},
    ImVec4{.98f,.85f,.36f,1.f}, ImVec4{.42f,.73f,1.f,1.f}};

std::string blockLabel(std::string_view value) {
    auto name = verifierBlockParts(value).first;
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    return std::string{name};
}
std::string fittedText(std::string text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    while (!text.empty() && ImGui::CalcTextSize((text + "...").c_str()).x > width) {
        auto start = text.size() - 1;
        while (start && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) --start;
        text.erase(start);
    }
    return text + "...";
}
void sameLineIfFits(float width, UiMetrics const& metrics) {
    if (ImGui::GetItemRectMax().x + metrics.gap + width <=
        ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
        ImGui::SameLine(0, metrics.gap);
}
float buttonWidth(char const* label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
}
bool samePair(Mismatch const& a, Mismatch const& b) {
    return a.kind == b.kind && a.expected == b.expected && a.actual == b.actual;
}
} // namespace

void renderVerificationPage(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics) {
    using schematic::VerificationPhase;
    auto const tr = [](TextKey key) { return i18n::tr(key); };
    auto const& snap = model.schematic;
    if (!model.verifierView) model.verifierView = std::make_shared<VerifierViewState>();
    auto& view = *model.verifierView;
    auto const report = snap.report; // Immutable snapshot held for this entire frame.
    view.update(report, snap.filter);
    auto const placement = selectedPlacement(snap.session.document);
    ImGui::TextWrapped("%s", placement ? placement->name.c_str() : tr(TextKey::VerifierNoPlacement));
    bool const busy = snap.phase == VerificationPhase::Queued || snap.phase == VerificationPhase::Running;
    bool const canVerify = snap.worldAvailable && placement && snap.session.writable && snap.activeProjectionAvailable && !busy;
    ImGui::BeginDisabled(!canVerify || !actions.verifySchematic);
    if (ImGui::Button(tr(TextKey::VerifierStart)) && canVerify && actions.verifySchematic) {
        actions.verifySchematic(); ImGui::EndDisabled(); return;
    }
    ImGui::EndDisabled();
    sameLineIfFits(buttonWidth(tr(TextKey::VerifierCancel)), metrics);
    ImGui::BeginDisabled(!busy || !actions.cancelVerification);
    if (ImGui::Button(tr(TextKey::VerifierCancel)) && busy && actions.cancelVerification) {
        actions.cancelVerification(); ImGui::EndDisabled(); return;
    }
    ImGui::EndDisabled();
    sameLineIfFits(buttonWidth(tr(TextKey::VerifierReset)), metrics);
    ImGui::BeginDisabled(!actions.resetVerification || (!report && snap.phase == VerificationPhase::NotVerified));
    if (ImGui::Button(tr(TextKey::VerifierReset)) && actions.resetVerification) {
        actions.resetVerification(); view.search.fill(0); view.cachedReport.reset();
        ImGui::EndDisabled(); return;
    }
    ImGui::EndDisabled();
    constexpr std::array phaseKeys{TextKey::VerifierIdle, TextKey::VerifierWaiting, TextKey::VerifierRunning,
        TextKey::VerifierDone, TextKey::VerifierStopped};
    auto const phaseLabel = tr(phaseKeys[static_cast<std::size_t>(snap.phase)]);
    sameLineIfFits(ImGui::CalcTextSize(phaseLabel).x + 180.f * metrics.scale, metrics);
    if (busy && report) {
        char progress[80]{}; std::snprintf(progress, sizeof(progress), "%s %.0f%%", phaseLabel,
            std::clamp(report->progress, 0.f, 1.f) * 100.f);
        ImGui::ProgressBar(std::clamp(report->progress, 0.f, 1.f), ImVec2(-1, 0), progress);
    } else ImGui::TextUnformatted(phaseLabel);
    ImGui::TextWrapped("%s", tr(TextKey::VerifierManualHint));
    if (!snap.status.empty()) ImGui::TextWrapped("%s", presentationStatus(snap.status).c_str());
    ImGui::Separator();
    if (ImGui::RadioButton(tr(TextKey::VerifierAll), !view.errorsOnly)) view.errorsOnly = false;
    sameLineIfFits(buttonWidth(tr(TextKey::VerifierErrorsOnly)), metrics);
    if (ImGui::RadioButton(tr(TextKey::VerifierErrorsOnly), view.errorsOnly)) view.errorsOnly = true;
    if (busy) {
        if (report) ImGui::Text(tr(TextKey::VerifierChecked), static_cast<unsigned long long>(report->checked));
        ImGui::TextWrapped("%s", tr(snap.phase == VerificationPhase::Queued ? TextKey::VerifierQueued : TextKey::SchematicPending));
        return;
    }
    if (!report) {
        ImGui::TextWrapped("%s", tr(snap.phase == VerificationPhase::Cancelled ? TextKey::VerifierCancelled : TextKey::VerifierNotVerified));
        return;
    }
    auto const& tally = report->tally;
    auto const errors = tally.missing + tally.wrongType + tally.wrongState + tally.extra;
    ImGui::TextWrapped(tr(TextKey::VerifierTotals), static_cast<unsigned long long>(errors),
        static_cast<unsigned long long>(tally.correct), static_cast<unsigned long long>(tally.unknown + tally.unknownAir));
    constexpr std::array filters{MistakeFilter::Mistakes, MistakeFilter::Missing, MistakeFilter::WrongType, MistakeFilter::WrongState, MistakeFilter::Extra};
    constexpr std::array filterKeys{TextKey::SchematicFilterAll, TextKey::VerifierMissing, TextKey::VerifierWrongType, TextKey::VerifierWrongState, TextKey::VerifierExtra};
    auto filter = snap.filter;
    char const* filterLabel = tr(TextKey::SchematicFilterWrong);
    for (std::size_t i = 0; i < filters.size(); ++i) if (filter == filters[i]) filterLabel = tr(filterKeys[i]);
    ImGui::SetNextItemWidth(metrics.compact ? -FLT_MIN : 230.f * metrics.scale);
    ImGui::BeginDisabled(!actions.setMistakeFilter);
    if (ImGui::BeginCombo("##VerifierCategoryFilter", filterLabel)) {
        for (std::size_t i = 0; i < filters.size(); ++i) {
            if (ImGui::Selectable(tr(filterKeys[i]), filter == filters[i]) && actions.setMistakeFilter) {
                actions.setMistakeFilter(filters[i]); filter = filters[i];
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (!metrics.compact) ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##VerifierSearch", tr(TextKey::VerifierSearch), view.search.data(), view.search.size());
    // A filter action changes the report stamp. Defer row actions until next snapshot.
    if (filter != snap.filter) return;
    view.update(report, filter);
    auto const searching = !view.cachedSearch.empty();
    ImGui::TextWrapped(tr(TextKey::VerifierFilterStatus), tr(view.errorsOnly ? TextKey::VerifierErrorsOnly : TextKey::VerifierAll),
        filterLabel, searching ? view.search.data() : tr(TextKey::VerifierNoSearch));
    ImGui::TextWrapped("%s", tr(TextKey::VerifierCountHint));
    if (report->truncated) ImGui::TextWrapped("%s", tr(TextKey::SchematicTruncated));
    auto const line = ImGui::GetTextLineHeightWithSpacing();
    bool const sideBySide = !metrics.compact && ImGui::GetContentRegionAvail().x >= 800.f * metrics.scale;
    auto const panelHeight = sideBySide ? std::max(line * 10, ImGui::GetContentRegionAvail().y - metrics.gap) : line * 12;
    auto const listWidth = sideBySide ? ImGui::GetContentRegionAvail().x * .58f : 0.f;
    if (ImGui::BeginChild("##MismatchList", ImVec2(listWidth, panelHeight), ImGuiChildFlags_Borders)) {
        if (!view.errorsOnly && filter == MistakeFilter::Mistakes && !searching) {
            for (auto const normal : {true, false}) {
                auto label = std::string{tr(normal ? TextKey::VerifierCorrect : TextKey::VerifierUnknown)} + " (" +
                    std::to_string(normal ? tally.correct : tally.unknown + tally.unknownAir) + ")";
                ImGui::PushStyleColor(ImGuiCol_Text, normal ? ImVec4{.45f,.87f,.60f,1.f} : ImVec4{.68f,.71f,.78f,1.f});
                bool const open = ImGui::CollapsingHeader(label.c_str()); ImGui::PopStyleColor();
                if (open) ImGui::TextWrapped("%s", tr(TextKey::VerifierSummaryOnly));
            }
        }
        std::array counts{tally.missing, tally.wrongType, tally.wrongState, tally.extra};
        std::size_t visibleGroups{};
        for (std::size_t category = 0; category < kinds.size(); ++category) {
            auto const& indices = view.categories[static_cast<std::size_t>(kinds[category])];
            visibleGroups += indices.size();
            if (!matchesFilter(kinds[category], filter)) continue;
            ImGui::PushID(static_cast<int>(kinds[category]));
            auto label = std::string{tr(categoryKeys[category])} + " (" + std::to_string(counts[category]) + ")###Category";
            ImGui::PushStyleColor(ImGuiCol_Text, categoryColors[category]);
            bool const open = ImGui::CollapsingHeader(label.c_str(), indices.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor();
            if (open) {
                if (!indices.empty() && ImGui::BeginTable("##VerifierPairs", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                    bool const narrow = ImGui::GetContentRegionAvail().x < 460.f * metrics.scale;
                    ImGui::TableSetupColumn(tr(narrow ? TextKey::VerifierExpectedShort : TextKey::VerifierExpected), ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(tr(narrow ? TextKey::VerifierActualShort : TextKey::VerifierActual), ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(tr(TextKey::VerifierShownCount), ImGuiTableColumnFlags_WidthFixed,
                        ImGui::CalcTextSize(tr(TextKey::VerifierShownCount)).x + metrics.gap);
                    ImGui::TableHeadersRow();
                    ImGuiListClipper clipper; clipper.Begin(static_cast<int>(indices.size()));
                    while (clipper.Step()) for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                        auto const& group = view.groups[indices[n]];
                        auto const& row = report->mismatches[group.indices.front()];
                        ImGui::PushID(static_cast<int>(group.indices.front()));
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                        auto const width = ImGui::GetContentRegionAvail().x;
                        bool const selected = snap.target && samePair(*snap.target, row);
                        if (ImGui::Selectable("##MismatchRow", selected,
                            ImGuiSelectableFlags_SpanAllColumns | (!actions.selectMistake ? ImGuiSelectableFlags_Disabled : 0),
                            ImVec2(0, ImGui::GetTextLineHeight())) && actions.selectMistake)
                            actions.selectMistake(report->stamp, group.indices.front());
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(450.f * metrics.scale);
                            ImGui::TextWrapped("%s: %s\n%s: %s", tr(TextKey::VerifierExpected), row.expected.c_str(), tr(TextKey::VerifierActual), row.actual.c_str());
                            ImGui::PopTextWrapPos(); ImGui::EndTooltip();
                        }
                        auto const text = fittedText(blockLabel(row.expected), width);
                        ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(), ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(fittedText(blockLabel(row.actual), ImGui::GetContentRegionAvail().x).c_str());
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", group.indices.size());
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::PopID();
        }
        if (!visibleGroups) ImGui::TextWrapped("%s", tr(TextKey::VerifierNoRows));
    }
    ImGui::EndChild();
    if (sideBySide) ImGui::SameLine(0, metrics.gap); else ImGui::Spacing();
    auto const detailFlags = ImGuiChildFlags_Borders | (sideBySide ? 0 : ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize);
    if (ImGui::BeginChild("##VerifierDetails", ImVec2(0, sideBySide ? panelHeight : 0), detailFlags,
        sideBySide ? 0 : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::TextUnformatted(tr(TextKey::VerifierDetails)); ImGui::Separator();
        auto selected = view.groups.end();
        if (snap.target) selected = std::find_if(view.groups.begin(), view.groups.end(), [&](auto const& group) {
            return samePair(*snap.target, report->mismatches[group.indices.front()]);
        });
        if (selected == view.groups.end()) ImGui::TextWrapped("%s", tr(TextKey::VerifierSelectHint));
        else {
            auto const& row = report->mismatches[selected->indices.front()];
            ImGui::TextWrapped("%s: %s", tr(TextKey::VerifierExpected), std::string{verifierBlockParts(row.expected).first}.c_str());
            auto const expectedState = verifierBlockParts(row.expected).second;
            auto const actualState = verifierBlockParts(row.actual).second;
            ImGui::TextWrapped("%s: %s", tr(TextKey::VerifierExpectedState), expectedState.empty() ? tr(TextKey::VerifierNoState) : std::string{expectedState}.c_str());
            ImGui::TextWrapped("%s: %s", tr(TextKey::VerifierActual), std::string{verifierBlockParts(row.actual).first}.c_str());
            ImGui::TextWrapped("%s: %s", tr(TextKey::VerifierActualState), actualState.empty() ? tr(TextKey::VerifierNoState) : std::string{actualState}.c_str());
            ImGui::Text("%s: %zu", tr(TextKey::VerifierRetained), selected->indices.size());
            if (searching && !verifierMatchesSearch(row, view.cachedSearch)) ImGui::TextWrapped("%s", tr(TextKey::VerifierSelectionHidden));
            ImGui::BeginDisabled(!actions.clearMistakeTarget);
            if (ImGui::Button(tr(TextKey::VerifierClearHighlight)) && actions.clearMistakeTarget) actions.clearMistakeTarget();
            ImGui::EndDisabled();
            ImGui::TextWrapped("%s", tr(TextKey::VerifierCoordinates));
            if (ImGui::BeginChild("##VerifierCoordinates", ImVec2(0, line * 6), ImGuiChildFlags_Borders)) {
                ImGuiListClipper clipper; clipper.Begin(static_cast<int>(selected->indices.size()));
                while (clipper.Step()) for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                    auto const index = selected->indices[n]; auto const& coordinate = report->mismatches[index];
                    char label[160]{}; std::snprintf(label, sizeof(label), "%lld, %lld, %lld  (%.1f)",
                        coordinate.world.x, coordinate.world.y, coordinate.world.z, std::sqrt(coordinate.distanceSquared));
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Selectable(label, snap.target && schematic::sameMismatch(*snap.target, coordinate),
                        !actions.selectMistake ? ImGuiSelectableFlags_Disabled : 0) && actions.selectMistake)
                        actions.selectMistake(report->stamp, index);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::BeginDisabled(view.groups.empty() || !actions.cycleMistake);
        if (ImGui::Button(tr(TextKey::SchematicNearest)) && actions.cycleMistake) actions.cycleMistake(filter);
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", tr(TextKey::VerifierCoordinateHint));
    }
    ImGui::EndChild();
}
} // namespace lholo::ui
