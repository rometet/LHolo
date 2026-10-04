#include "structure/StructureUiState.h"
#include "ui/MaterialsPage.h"
#include "ui/MenuPages.h"
#include "i18n/LanguageStore.h"
#include <imgui_internal.h>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace {
std::size_t checks{};
void check(bool ok,char const* message) {++checks;if(!ok)throw std::runtime_error(message);}
using namespace lholo::structure::detail;
std::vector<MaterialRequirement> fixture() {
    return {{"石",{},"minecraft:stone","minecraft:stone",128,64,"body:item:stone|item=stone"},
        {"同じ名前",{},"state_a","item_a",32,64,"body:state_a|item=item_a"},
        {"同じ名前",{},"state_b","item_b",16,64,"body:state_b|item=item_b"},
        {"水",{},"minecraft:water",{},8,64,"liquid:water|item="}};
}
std::string read(std::filesystem::path const& path) {std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
void testState() {
    auto& state=StructureUiState::getInstance();state.clearMaterials();auto const token=state.materialListToken();
    check(state.publishMaterialList(fixture(),{64,32,4,std::nullopt},101,"fixture.mcstructure",token),"initial publication");
    auto const first=state.materialListView();
    check(first && first->requirements.size()==4,"complete immutable snapshot");
    check(first->shortage(0)==64 && first->shortage(1)==0 && !first->shortage(3),"known/unknown shortage");
    check(state.replaceMaterialHudSnapshot(fixture(),{64,32,4,0}),"HUD fixture");auto const hud=state.materialHudView();
    check(state.setMaterialIgnored(first->scope,first->requirements[1].key,true),"ignore exact identity");
    auto const ignored=state.materialListView();
    check(ignored->isIgnored(1) && !ignored->isIgnored(2),"same display name preserves state/item keys");
    check(first->ignored.empty(),"captured old snapshot immutable");
    check(state.materialHudView()==hud,"ignore leaves HUD untouched");
    auto summary=summarizeMaterials(*ignored);
    check(summary.original==184 && summary.working==152 && summary.excluded==32 && summary.excludedKinds==1 && summary.unknownKinds==1,"honest original/working totals");
    check(!ignored->matches(1,MaterialFilter::Shortage) && ignored->matches(1,MaterialFilter::Ignored),"ignored excluded from shortages");
    check(!ignored->matches(3,MaterialFilter::Shortage),"unknown is not zero");
    check(!state.setMaterialIgnored({102,token},first->requirements[0].key,true),"wrong generation rejected");
    check(!state.setMaterialIgnored(first->scope,"same display name",true),"display name not key");
    check(state.setMaterialListAvailability(first->scope,{0,4,16,std::nullopt}),"tick inventory publication");
    check(state.materialListView()->isIgnored(1),"availability retains ignore");
    check(state.clearMaterialIgnored(first->scope) && state.materialListView()->ignored.empty(),"restore all");
    check(state.setMaterialIgnored(first->scope,first->requirements[0].key,true),"re-ignore");
    check(state.setMaterialIgnored(first->scope,first->requirements[0].key,false),"single restore");
    state.clearMaterials();auto const nextToken=state.materialListToken();
    check(!state.publishMaterialList(fixture(),{},101,"old",token),"stale async publication rejected");
    check(state.publishMaterialList(fixture(),{},102,"next",nextToken),"next generation");
    check(state.materialListView()->ignored.empty() && !state.setMaterialIgnored(first->scope,first->requirements[0].key,true),"old actions cannot affect next generation");
    state.resetWorldSession();check(!state.materialListView(),"world reset retires list");
    check(state.publishMaterialList(fixture(),{64,32,4,std::nullopt},103,"再読込.mcstructure",state.materialListToken()),"new world snapshot");
}
lholo::io::MaterialExportRequest request() {
    lholo::io::MaterialExportRequest value;
    value.snapshot=StructureUiState::getInstance().materialListView();value.filter=MaterialFilter::All;
    value.names={"日本語\t欄\n改行\"引用\"","=SUM(1,2)","同名","水"};value.capturedAt="fixture UTC";return value;
}
void waitForExport(lholo::io::MaterialExportJob& job) {
    auto const deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    while(job.poll().phase==lholo::io::MaterialExportPhase::Saving) {
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("fixture export deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}
void testExport(std::filesystem::path const& dir) {
    using namespace lholo::io;
    auto path=dir/L"日本語の材料.tsv";auto captured=request();writeMaterialTsv(path,captured);auto bytes=read(path);
    check(bytes.starts_with("\xEF\xBB\xBF"),"UTF-8 BOM");
    check(bytes.find("日本語\t欄\n改行\"\"引用\"\"")!=std::string::npos,"TSV multiline/tab/quotes");
    check(bytes.find("\"'=SUM(1,2)\"")!=std::string::npos,"spreadsheet formula escaped");
    check(bytes.find("\"liquid:water|item=\"\t\"minecraft:water\"\t\"\"\t8\t\t\tfalse\t8")!=std::string::npos,"unknown counts empty, not zero");
    check(bytes.find("original_total\t184")!=std::string::npos && bytes.find("generation\t103")!=std::string::npos,"original totals and generation metadata");
    MaterialExportJob job;std::atomic_bool release{};auto const owner=std::this_thread::get_id();std::thread::id worker;
    check(job.start(captured,[&](auto const&){worker=std::this_thread::get_id();while(!release.load())std::this_thread::yield();return std::optional{path};}),"start worker");
    check(job.poll().phase==MaterialExportPhase::Saving && !job.start(captured,[](auto const&){return std::optional<std::filesystem::path>{};}),"poll nonblocking and no duplicate job");
    auto& state=StructureUiState::getInstance();state.clearMaterials();release=true;waitForExport(job);
    check(job.poll().phase==MaterialExportPhase::Saved && job.poll().source==captured.snapshot->source && worker!=owner && read(path)==bytes,"click-time immutable snapshot survives reload; worker owns disk");
    check(job.start(captured,[](auto const&){return std::optional<std::filesystem::path>{};}),"cancel fixture");waitForExport(job);
    check(job.poll().phase==MaterialExportPhase::Cancelled && read(path)==bytes,"cancel writes nothing");
    check(job.start(captured,[&](auto const&){return std::optional{dir/"absent"/"cannot.tsv"};}),"failure fixture");waitForExport(job);
    check(job.poll().phase==MaterialExportPhase::Failed && !job.poll().destination.empty() && !job.poll().error.empty(),"failure retains destination and error");
    check(job.closeAndDrain(std::chrono::milliseconds{0}) && !job.poll().accepting,"completed export drain closes admission");
    auto cancellation=std::make_shared<MaterialExportCancellation>();cancellation->cancel();
    check(!writeMaterialTsv(path,captured,cancellation) && read(path)==bytes,"pre-cancelled writer preserves existing file");
    check(!writeMaterialTsv(dir/"pre-cancel-no-file.tsv",captured,cancellation)
        && !std::filesystem::exists(dir/"pre-cancel-no-file.tsv"),"pre-cancelled writer creates no output");
    auto const original=read(path);auto invalid=captured;invalid.names.clear();bool failed{};
    try{writeMaterialTsv(path,invalid);}catch(...){failed=true;}
    check(failed && read(path)==original,"invalid output does not truncate existing file");
    auto locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"fixture destination locked");failed=false;
    auto replacement=captured;replacement.names[0]="different snapshot";
    try{writeMaterialTsv(path,replacement);}catch(...){failed=true;}
    CloseHandle(locked);check(failed && read(path)==original,"failed atomic replacement preserves original bytes");
    for(auto const& entry:std::filesystem::directory_iterator(dir))check(!entry.path().filename().string().starts_with(".lholo-output-"),"failure cleans staged output");
    captured.filter=MaterialFilter::Shortage;writeMaterialTsv(dir/"shortage.tsv",captured);
    check(read(dir/"shortage.tsv").find("liquid:water")==std::string::npos,"export respects current filter");
}
void testUi() {
    using namespace lholo::ui;auto& state=StructureUiState::getInstance();state.clearMaterials();
    check(state.publishMaterialList(fixture(),{64,32,4,std::nullopt},104,"UI fixture",state.materialListToken()),"UI live-owner fixture");
    auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize={1400,2400};io.DeltaTime=1.f/60.f;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontDefault();check(io.Fonts->Build(),"UI font");lholo::i18n::setLanguageByCode("en_US");
    MenuModel model;model.hasLoadedStructure=true;model.materialsView=std::make_shared<MaterialsViewState>();
    MenuActions actions;int exports{};actions.ignoreMaterial=[&](auto scope,auto const& key,bool value){check(state.setMaterialIgnored(scope,key,value),"UI ignore connected");};
    actions.clearIgnoredMaterials=[&](auto scope){check(state.clearMaterialIgnored(scope),"UI restore connected");};
    actions.exportMaterials=[&](auto value){++exports;check(value.snapshot==model.materialList && value.filter==model.materialsView->filter,"UI exports visible captured snapshot");};
    actions.requestMaterials=[]{};auto const metrics=calculateMetrics(io.DisplaySize,1);applyFluentTheme(metrics);
    bool filePage{};
    auto frame=[&]{model.materialList=state.materialListView();ImGui::NewFrame();ImGui::SetNextWindowPos({10,10});ImGui::SetNextWindowSize({1200,2300});
        ImGui::Begin("MaterialsTests");if(filePage)renderProjectionPage(model,actions,metrics);else renderMaterialsPage(model,actions,metrics);ImGui::End();ImGui::Render();check(context->ErrorCountCurrentFrame==0,"ImGui diagnostics");};
    frame();frame();auto* window=ImGui::FindWindowByName("MaterialsTests");check(window!=nullptr,"UI window");
    auto activate=[&](lholo::i18n::TextKey key){context->NavNextActivateId=window->GetID(lholo::i18n::tr(key));frame();};
    activate(lholo::i18n::TextKey::MaterialsShortage);check(model.materialsView->filter==MaterialFilter::Shortage && model.materialsView->visible.size()==2,"shortage radio real action");
    activate(lholo::i18n::TextKey::MaterialsExport);check(exports==1,"export button real action");
    activate(lholo::i18n::TextKey::MaterialsAll);
    auto* card=ImGui::FindWindowByName("MaterialsTests/##Materials");
    if(!card)for(auto* candidate:context->Windows)if(std::string_view(candidate->Name).find("/##Materials_")!=std::string_view::npos
        && std::string_view(candidate->Name).find("MaterialsWorkTable")==std::string_view::npos){card=candidate;break;}
    check(card!=nullptr,"material card");
    auto* table=context->Tables.GetByKey(card->GetID("##MaterialsWorkTable"));check(table && table->InnerWindow,"material table");
    auto const tableSeed=table->ID;auto const snapshot=state.materialListView();
    auto const identity=std::to_string(snapshot->scope.generation)+":"+std::to_string(snapshot->scope.token)+":"+snapshot->requirements[0].key;
    auto const rowSeed=ImHashStr(identity.c_str(),0,tableSeed);
    context->NavNextActivateId=ImHashStr("##MaterialIgnored",0,rowSeed);frame();frame();
    check(state.materialListView()->isIgnored(0),"checkbox through production state operation");
    activate(lholo::i18n::TextKey::MaterialsIgnored);check(model.materialsView->visible.size()==1,"ignored filter");
    activate(lholo::i18n::TextKey::MaterialsRestore);frame();check(state.materialListView()->ignored.empty(),"restore button real action");
    model.materialExport.accepting=false;frame();
    auto const stoppedExports=exports;activate(lholo::i18n::TextKey::MaterialsExport);
    check(exports==stoppedExports,"closed lifecycle gate disables production export button");
    model.materialExport.accepting=true;
    auto huge=std::make_shared<MaterialListSnapshot>(*state.materialListView());huge->requirements.clear();huge->available.clear();
    for(int i=0;i<50000;++i)huge->requirements.push_back({"large",{},"block_"+std::to_string(i),"item",1,64,"key_"+std::to_string(i)});
    check(state.publishMaterialList(huge->requirements,{},105,"large",state.materialListToken()),"large result");
    auto const oldCheckbox=ImHashStr("##MaterialIgnored",0,rowSeed);
    context->NavNextActivateId=oldCheckbox;frame();frame();check(state.materialListView()->ignored.empty(),"old generation navigation ID cannot ignore a new material");
    auto const rebuilds=model.materialsView->rebuilds;frame();
    check(model.materialsView->rebuilds==rebuilds && model.materialsView->visible.size()==50000 && model.materialsView->drawnRows<100,"50000 rows cached and clipped");
    filePage=true;model.page=MenuPage::Projection;frame();frame();
    ImGuiWindow* fileCard{};for(auto* candidate:context->Windows)if(std::string_view(candidate->Name).find("/##ProjectionFile_")!=std::string_view::npos){fileCard=candidate;break;}
    check(fileCard!=nullptr,"file card");context->NavNextActivateId=fileCard->GetID(lholo::i18n::tr(lholo::i18n::TextKey::MaterialListTitle));frame();
    check(model.page==MenuPage::Materials && !model.materialPopupRequested,"legacy materials button reaches functional work page");
    resetFluentTheme();ImGui::DestroyContext(context);
}
}
int main(int argc,char** argv) {
    try {lholo::i18n::initLanguageStore();auto dir=std::filesystem::path(argc>1?argv[1]:"build/material-fixtures");std::filesystem::create_directories(dir);
        testState();testExport(dir);testUi();std::printf("Materials tests: %zu checks PASS\n",checks);return 0;
    }catch(std::exception const& error){std::fprintf(stderr,"Materials failure after %zu checks: %s\n",checks,error.what());return 1;}
}
