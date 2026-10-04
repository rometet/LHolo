#include "ui/MaterialsPage.h"
#include "ui/MenuPages.h"
#include "ui/MenuWidgets.h"
#include <array>
#include <cstdio>
#include <ctime>

namespace lholo::ui {
namespace {
using namespace structure::detail;
using i18n::TextKey;
char const* textFor(TextKey key) {return i18n::tr(key);}
void nextIfFits(char const* label,UiMetrics const& metrics,bool radio=false) {
    auto const width=ImGui::CalcTextSize(label).x+(radio ? ImGui::GetFrameHeight()+ImGui::GetStyle().ItemInnerSpacing.x : ImGui::GetStyle().FramePadding.x*2);
    if(ImGui::GetItemRectMax().x+metrics.gap+width<=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x)
        ImGui::SameLine(0,metrics.gap);
}
void fitted(char const* text,bool tooltip=true) {
    auto const width=ImGui::GetContentRegionAvail().x;
    std::string visible{text};
    if(ImGui::CalcTextSize(text).x>width) {
        while(!visible.empty() && ImGui::CalcTextSize((visible+"...").c_str()).x>width) {
            auto start=visible.size()-1;
            while(start && (static_cast<unsigned char>(visible[start])&0xC0)==0x80)--start;
            visible.erase(start);
        }
        visible+="...";
    }
    ImGui::TextUnformatted(visible.c_str());
    if(tooltip && ImGui::IsItemHovered()) {ImGui::BeginTooltip();ImGui::TextUnformatted(text);ImGui::EndTooltip();}
}
std::shared_ptr<MaterialListSnapshot const> listFor(MenuModel const& model) {
    if(model.materialList)return model.materialList;
    if(model.materials.empty())return {};
    // Legacy read-only popup fixtures, kept separate from the live state API.
    auto snapshot=std::make_shared<MaterialListSnapshot>();
    for(auto const& row:model.materials)
        snapshot->requirements.push_back({row.displayName,row.nameKey,row.typeName,{},row.count,row.stackSize,{}});
    return snapshot;
}
char const* nameFor(MaterialRequirement const& row) {return row.nameKey ? textFor(*row.nameKey) : row.displayName.c_str();}
std::string nowUtc() {
    auto const clock=std::time(nullptr);std::tm time{};gmtime_s(&time,&clock);char buffer[40]{};
    std::strftime(buffer,sizeof(buffer),"%Y-%m-%dT%H:%M:%SZ",&time);return buffer;
}
void exportStatus(io::MaterialExportResult const& result) {
    using io::MaterialExportPhase;
    if(!result.accepting)ImGui::TextWrapped("%s",textFor(TextKey::MaterialsExportStopping));
    if(result.phase==MaterialExportPhase::Saving)ImGui::TextWrapped("%s",textFor(TextKey::MaterialsSaving));
    if(result.phase==MaterialExportPhase::Saved)ImGui::TextWrapped(textFor(TextKey::MaterialsSaved),result.source.c_str());
    if(result.phase==MaterialExportPhase::Cancelled)ImGui::TextWrapped("%s",textFor(TextKey::MaterialsCancelled));
    if(result.phase==MaterialExportPhase::Failed) {
        ImGui::TextWrapped("%s",textFor(TextKey::MaterialsFailed));ImGui::TextWrapped("%s",result.error.c_str());
    }
    if(!result.destination.empty()) {
        auto const utf8=result.destination.u8string();std::string path(reinterpret_cast<char const*>(utf8.data()),utf8.size());
        ImGui::TextWrapped(textFor(TextKey::MaterialsDestination),path.c_str());
    }
}
}

void renderMaterialBill(MenuModel const& model,MenuActions const& actions,UiMetrics const& metrics,float areaHeight,bool interactive) {
    auto const snapshot=listFor(model);
    if(!model.hasLoadedStructure) {ImGui::TextDisabled("%s",textFor(TextKey::StatusNotLoaded));return;}
    if(!snapshot) {ImGui::TextWrapped("%s",textFor(TextKey::MaterialsLoading));return;}
    static MaterialsViewState popupView;
    MaterialsViewState localView;
    auto& view=interactive && model.materialsView ? *model.materialsView : interactive ? localView : popupView;
    view.update(snapshot);view.drawnRows=0;
    auto const& summary=view.summary;
    ImGui::TextWrapped(textFor(TextKey::MaterialsOriginal),static_cast<unsigned long long>(summary.original),snapshot->requirements.size());
    ImGui::TextWrapped(textFor(TextKey::MaterialsWorking),static_cast<unsigned long long>(summary.working),
        static_cast<unsigned long long>(summary.excluded),summary.excludedKinds);
    if(summary.unknownKinds)ImGui::TextWrapped(textFor(TextKey::MaterialsUnknownKinds),summary.unknownKinds);
    ImGui::TextWrapped(textFor(TextKey::MaterialsShown),view.visible.size(),snapshot->requirements.size());
    if(view.visible.empty()) {ImGui::TextWrapped("%s",textFor(TextKey::MaterialsEmpty));return;}
    auto const rowHeight=ImGui::GetTextLineHeightWithSpacing()*3+ImGui::GetStyle().CellPadding.y*2;
    auto const height=areaHeight>0 ? std::max(rowHeight+ImGui::GetFrameHeight(),areaHeight) : std::max(1.f,ImGui::GetContentRegionAvail().y);
    if(ImGui::BeginTable("##MaterialsWorkTable",3,ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,ImVec2(0,height))) {
        ImGui::TableSetupColumn(textFor(TextKey::ColumnItem),ImGuiTableColumnFlags_WidthStretch,1.f);
        ImGui::TableSetupColumn(textFor(TextKey::MaterialsCounts),ImGuiTableColumnFlags_WidthStretch,1.f);
        auto const ignoreWidth=ImGui::CalcTextSize(textFor(TextKey::MaterialsIgnore)).x+ImGui::GetStyle().FramePadding.x*2;
        ImGui::TableSetupColumn(textFor(TextKey::MaterialsIgnore),ImGuiTableColumnFlags_WidthFixed,ignoreWidth);
        ImGui::TableSetupScrollFreeze(0,1);
        // Fixed-height rows with tooltips permit the clipper to skip thousands
        // of materials without measuring/wrapping every name on every frame.
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for(int column=0;column<3;++column) {ImGui::TableSetColumnIndex(column);fitted(ImGui::TableGetColumnName(column));}
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(view.visible.size()),rowHeight);
        while(clipper.Step())for(int visible=clipper.DisplayStart;visible<clipper.DisplayEnd;++visible) {
            auto const index=view.visible[static_cast<std::size_t>(visible)];auto const& row=snapshot->requirements[index];
            auto const identity=std::to_string(snapshot->scope.generation)+":"+std::to_string(snapshot->scope.token)+":"+row.key;
            ++view.drawnRows;ImGui::PushID(identity.c_str());
            ImGui::TableNextRow(ImGuiTableRowFlags_None,rowHeight);
            ImGui::TableSetColumnIndex(0);fitted(nameFor(row));
            ImGui::TextDisabled("%s",snapshot->isIgnored(index)?textFor(TextKey::MaterialsIgnored):"");
            // Clip the identifier, while exposing the exact block/item/key in
            // a tooltip. No identity gets truncated in an export or an action.
            fitted(row.typeName.c_str(),false);
            if(ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();ImGui::TextUnformatted(row.typeName.c_str());
                ImGui::TextUnformatted(row.itemId.c_str());ImGui::TextUnformatted(row.key.c_str());ImGui::EndTooltip();
            }
            ImGui::TableSetColumnIndex(1);
            char count[100]{};std::snprintf(count,sizeof(count),textFor(TextKey::MaterialsRequired),static_cast<unsigned long long>(row.count));fitted(count);
            if(index<snapshot->available.size() && snapshot->available[index]) {
                std::snprintf(count,sizeof(count),textFor(TextKey::MaterialsOwned),std::max(0,*snapshot->available[index]));fitted(count);
                std::snprintf(count,sizeof(count),textFor(TextKey::MaterialsMissing),static_cast<unsigned long long>(*snapshot->shortage(index)));fitted(count);
            } else {fitted(textFor(TextKey::MaterialsOwnedUnknown));fitted(textFor(TextKey::MaterialsMissingUnknown));}
            ImGui::TableSetColumnIndex(2);bool ignored=snapshot->isIgnored(index);
            ImGui::BeginDisabled(!interactive || !actions.ignoreMaterial || !snapshot->scope.generation || row.key.empty());
            if(ImGui::Checkbox("##MaterialIgnored",&ignored) && actions.ignoreMaterial)
                actions.ignoreMaterial(snapshot->scope,row.key,ignored);
            ImGui::EndDisabled();ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void renderMaterialsPage(MenuModel& model,MenuActions const& actions,UiMetrics const& metrics) {
    if(!model.materialsView)model.materialsView=std::make_shared<MaterialsViewState>();
    auto& view=*model.materialsView;view.update(model.materialList);
    if(model.materialList)ImGui::TextWrapped(textFor(TextKey::MaterialsSource),model.materialList->source.c_str());
    ImGui::TextWrapped("%s",textFor(TextKey::MaterialsScope));
    ImGui::TextWrapped("%s",textFor(TextKey::MaterialsInventoryHint));
    constexpr std::array keys{TextKey::MaterialsAll,TextKey::MaterialsShortage,TextKey::MaterialsIgnored};
    for(int filter=0;filter<3;++filter) {
        if(filter)nextIfFits(textFor(keys[filter]),metrics,true);
        if(ImGui::RadioButton(textFor(keys[filter]),static_cast<int>(view.filter)==filter))view.filter=static_cast<MaterialFilter>(filter);
    }
    auto const snapshot=model.materialList;
    ImGui::BeginDisabled(!model.hasLoadedStructure || !actions.requestMaterials);
    if(ImGui::Button(textFor(TextKey::MaterialRefresh)) && actions.requestMaterials)actions.requestMaterials();ImGui::EndDisabled();
    nextIfFits(textFor(TextKey::MaterialsRestore),metrics);
    ImGui::BeginDisabled(!snapshot || snapshot->ignored.empty() || !actions.clearIgnoredMaterials);
    if(ImGui::Button(textFor(TextKey::MaterialsRestore)) && snapshot && actions.clearIgnoredMaterials)actions.clearIgnoredMaterials(snapshot->scope);
    ImGui::EndDisabled();nextIfFits(textFor(TextKey::MaterialsExport),metrics);
    bool const canExport=snapshot && model.hasLoadedStructure && actions.exportMaterials
        && model.materialExport.accepting && model.materialExport.phase!=io::MaterialExportPhase::Saving;
    ImGui::BeginDisabled(!canExport);
    if(ImGui::Button(textFor(TextKey::MaterialsExport)) && canExport) {
        io::MaterialExportRequest request;request.snapshot=snapshot;request.filter=view.filter;request.capturedAt=nowUtc();
        request.names.reserve(snapshot->requirements.size());for(auto const& row:snapshot->requirements)request.names.emplace_back(nameFor(row));
        actions.exportMaterials(std::move(request));
    }
    ImGui::EndDisabled();ImGui::TextWrapped("%s",textFor(TextKey::MaterialsExportHint));
    exportStatus(model.materialExport);
    renderSection("##Materials",textFor(TextKey::MaterialListTitle),metrics,[&] {
        renderMaterialBill(model,actions,metrics,metrics.viewport.y*.42f,true);
    });
    // The existing selected-placement/Verifier material details retain their
    // separate meaning and are rendered unchanged below the work bill.
    renderSelectedMaterials(model);
}
}
