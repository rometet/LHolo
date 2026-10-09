#include "ui/LHoloMenu.h"
#include "ui/MenuPages.h"
#include "ui/MenuRoutePresentation.h"
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

void testComparisonControls() {
    using namespace lholo::ui;
    auto* context = ImGui::CreateContext();
    context->ErrorCallback = errorCallback;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {1400,2800}; io.DeltaTime = 1.f/60.f;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigErrorRecoveryEnableAssert = false;
    io.Fonts->AddFontDefault(); io.Fonts->Build();
    lholo::i18n::setLanguageByCode("en_US");
    MenuModel model;
    model.correctionFillOpacity = .23f; model.correctionOutlineOpacity = .74f;
    auto const metrics = calculateMetrics(io.DisplaySize, 1);
    applyFluentTheme(metrics);
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10,10}); ImGui::SetNextWindowSize({1200,2600});
        ImGui::Begin("ComparisonControlTest");
        renderRenderPage(model, {}, metrics);
        ImGui::End(); ImGui::Render();
        check(context->ErrorCountCurrentFrame == 0, "comparison controls scopes");
    };
    frame(); frame();
    ImGuiWindow* card{};
    for (auto* window : context->Windows) {
        if (std::string_view{window->Name}.find("##CorrectionStyle") != std::string_view::npos) card=window;
    }
    check(card != nullptr, "comparison style card exists");
    auto enterNumber = [&](char const* id, char const* text) {
        auto const item = card->GetID(id);
        context->NavNextActivateId = item;
        context->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
        frame();
        check(context->ActiveId == item, "comparison numeric control activates");
        io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); frame();
        io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(text); frame();
        io.AddKeyEvent(ImGuiKey_Enter, true); frame();
        io.AddKeyEvent(ImGuiKey_Enter, false); frame();
    };
    enterNumber("##ComparisonStrengthValue", "175");
    check(model.comparisonStrength == 1.75f, "comparison numeric strength applies");
    enterNumber("##CorrectionOutlineWidthValue", "6.5");
    check(model.correctionOutlineWidth == 6.5f, "comparison numeric width applies");
    enterNumber("##ComparisonStrengthValue", "500");
    check(model.comparisonStrength == 2.f, "comparison numeric strength clamps");
    enterNumber("##CorrectionOutlineWidthValue", "-9");
    check(model.correctionOutlineWidth == 1.f, "comparison numeric width clamps");
    auto activateReset = [&](char const* scope, lholo::i18n::TextKey key) {
        auto const id = ImHashStr(lholo::i18n::tr(key), 0, card->GetID(scope));
        context->NavNextActivateId = id; frame();
    };
    model.comparisonStrength=.5f; model.correctionOutlineWidth=7.f;
    activateReset("ComparisonStrength", lholo::i18n::TextKey::ButtonResetComparisonStrength);
    check(model.comparisonStrength == 1.f && model.correctionOutlineWidth == 7.f,
        "strength reset leaves outline width");
    activateReset("CorrectionOutlineWidth", lholo::i18n::TextKey::ButtonResetCorrectionOutlineWidth);
    check(model.correctionOutlineWidth == 1.f && model.correctionFillOpacity == .23f
        && model.correctionOutlineOpacity == .74f, "width reset leaves existing opacity settings");
    auto tweakSlider = [&](char const* id) {
        context->NavNextActivateId = card->GetID(id);
        context->NavNextActivateFlags = ImGuiActivateFlags_None;
        frame();
        io.AddKeyEvent(ImGuiKey_RightArrow, true); frame();
        io.AddKeyEvent(ImGuiKey_RightArrow, false); frame();
        io.AddKeyEvent(ImGuiKey_Escape, true); frame();
        io.AddKeyEvent(ImGuiKey_Escape, false); frame();
    };
    model.comparisonStrength=.5f;
    tweakSlider("##ComparisonStrengthSlider");
    check(model.comparisonStrength > .5f && model.comparisonStrength <= 2.f,
        "strength slider keyboard adjustment applies");
    tweakSlider("##CorrectionOutlineWidthSlider");
    check(model.correctionOutlineWidth > 1.f && model.correctionOutlineWidth <= 8.f,
        "outline width slider keyboard adjustment applies");
    resetFluentTheme(); ImGui::DestroyContext(context);
}

void testCompactNavigation() {
    using namespace lholo::ui;
    auto* context=ImGui::CreateContext();context->ErrorCallback=errorCallback;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize={640,480};io.DeltaTime=1.f/60.f;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.Fonts->AddFontDefault();io.Fonts->Build();
    lholo::i18n::setLanguageByCode("en_US");
    MenuModel model;MenuActions actions;std::size_t scans{};
    actions.verifySchematic=[&]{++scans;};
    auto const metrics=calculateMetrics(io.DisplaySize,1);applyFluentTheme(metrics);
    auto frame=[&]{ImGui::NewFrame();renderMenu(model,actions,metrics);ImGui::Render();
        check(context->ErrorCountCurrentFrame==0,"compact navigation scopes");};
    auto windowContaining=[&](char const* name){
        for(auto* window:context->Windows)if(window->Active && std::string_view(window->Name).find(name)!=std::string_view::npos)return window;
        return static_cast<ImGuiWindow*>(nullptr);
    };
    frame();frame();
    auto* panel=windowContaining("##MainPanel");check(panel!=nullptr,"compact main panel exists");
    for(auto page:{MenuPage::Schematics,MenuPage::Verification,MenuPage::Materials,MenuPage::Projection,
        MenuPage::CreateStructure,MenuPage::Render,MenuPage::Hud,MenuPage::Hotkeys,MenuPage::Interface,MenuPage::Experimental}) {
        context->NavNextActivateId=panel->GetID("##PageNavigation");
        context->NavNextActivateFlags=ImGuiActivateFlags_None;frame();
        auto* popup=windowContaining("##Combo_");check(popup!=nullptr,"compact page selector opens");
        // The settings routes near the bottom must also remain reachable.
        ImGui::SetScrollY(popup,page==MenuPage::Hotkeys || page==MenuPage::Interface || page==MenuPage::Experimental ? popup->ScrollMax.y : 0);
        frame();
        context->NavNextActivateId=popup->GetID(pageName(page));
        context->NavNextActivateFlags=ImGuiActivateFlags_None;frame();frame();
        check(model.page==page,"compact selector routes to the requested existing page");
        check(scans==0,"navigation never starts verification");
    }
    resetFluentTheme();ImGui::DestroyContext(context);
}

void testVerifierControls() {
    using namespace lholo::ui;
    using namespace lholo::structure;
    auto* context=ImGui::CreateContext();context->ErrorCallback=errorCallback;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize={1400,3600};io.DeltaTime=1.f/60.f;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.Fonts->AddFontDefault();io.Fonts->Build();
    lholo::i18n::setLanguageByCode("en_US");
    MenuModel model;model.schematic.worldAvailable=true;model.schematic.session.writable=true;
    SavedPlacement placement;placement.id=1;placement.file="test.mcstructure";placement.name="Test";
    model.schematic.session.document.placements={placement};model.schematic.session.document.selected=1;
    auto report=std::make_shared<schematic::Report>();
    report->stamp.worldEpoch=11;report->stamp.placementId=1;report->stamp.loadedGeneration=2;report->stamp.reportRevision=3;
    report->mismatches={{VerificationState::Missing,{1,2,3},"minecraft:stone","minecraft:air",9},
        {VerificationState::WrongType,{4,5,6},"minecraft:stone","minecraft:birch_planks",4},
        {VerificationState::WrongState,{7,8,9},"minecraft:oak_stairs [facing=south]","minecraft:oak_stairs [facing=north]",3},
        {VerificationState::Extra,{10,11,12},"minecraft:air","minecraft:stone",4},
        {VerificationState::Missing,{2,2,3},"minecraft:stone","minecraft:air",1}};
    report->tally.missing=21;report->tally.wrongType=22;report->tally.wrongState=23;report->tally.extra=24;
    model.schematic.report=report;model.schematic.phase=schematic::VerificationPhase::Completed;
    schematic::MistakeSelection selection;
    std::size_t rowCalls{},filterCalls{},verifyCalls{},cancelCalls{},resetCalls{},clearCalls{};
    MenuActions actions;
    actions.selectMistake=[&](schematic::ReportStamp const& stamp,std::size_t index){
        ++rowCalls;check(stamp==report->stamp,"UI forwards exact immutable report stamp");
        check(selection.select(stamp,report->stamp,false,report->mismatches,index),"UI forwards selectable source index");
        model.schematic.target=selection.target(report->stamp)->mismatch;
    };
    actions.setMistakeFilter=[&](MistakeFilter filter){
        ++filterCalls;check(selection.setFilter(filter),"UI forwards a valid category filter");
        model.schematic.filter=filter;model.schematic.target.reset();
        auto next=std::make_shared<schematic::Report>(*report);++next->stamp.filterRevision;
        report=std::move(next);model.schematic.report=report;
    };
    actions.verifySchematic=[&]{++verifyCalls;};
    actions.cancelVerification=[&]{++cancelCalls;};
    actions.resetVerification=[&]{++resetCalls;model.schematic.report.reset();model.schematic.target.reset();
        model.schematic.phase=schematic::VerificationPhase::NotVerified;selection.clear();};
    actions.clearMistakeTarget=[&]{++clearCalls;selection.clear();model.schematic.target.reset();};
    auto const metrics=calculateMetrics(io.DisplaySize,1);applyFluentTheme(metrics);
    auto frame=[&]{
        ImGui::NewFrame();ImGui::SetNextWindowPos({10,10});ImGui::SetNextWindowSize({1200,3400});
        ImGui::Begin("VerifierControlTest");renderVerificationPage(model,actions,metrics);ImGui::End();ImGui::Render();
        check(context->ErrorCountCurrentFrame==0,"Verifier UI scopes");
    };
    auto windowContaining=[&](char const* name){
        for(auto* window:context->Windows)if(std::string_view{window->Name}.find(name)!=std::string_view::npos)return window;
        return static_cast<ImGuiWindow*>(nullptr);
    };
    frame();frame();
    auto* rows=windowContaining("##MismatchList");check(rows!=nullptr,"Verifier list exists");
    auto selectRow=[&](VerificationState kind,int index){
        auto id=ImHashStr("##VerifierPairs",0,rows->GetID(static_cast<int>(kind)));
        auto* table=context->Tables.GetByKey(id);check(table!=nullptr,"Verifier category pair table exists");
        auto const seed=ImHashData(&index,sizeof(index),ImGui::TableGetInstanceID(table,0));
        context->NavNextActivateId=ImHashStr("##MismatchRow",0,seed);
        context->NavNextActivateFlags=ImGuiActivateFlags_None;frame();frame();
    };
    selectRow(VerificationState::WrongType,1);
    check(rowCalls==1 && model.schematic.target->world==Cell{4,5,6},"WrongType row selects highlight target");
    selectRow(VerificationState::Extra,3);
    check(rowCalls==2 && model.schematic.target->kind==VerificationState::Extra,"Extra row reselection");
    auto chooseFilter=[&](lholo::i18n::TextKey key,MistakeFilter filter,std::uint64_t count){
        auto* window=ImGui::FindWindowByName("VerifierControlTest");
        auto const label=std::string{lholo::i18n::tr(key)}+" ("+std::to_string(count)+")";
        context->NavNextActivateId=ImHashStr(label.c_str(),0,window->GetID(static_cast<int>(filter)));
        context->NavNextActivateFlags=ImGuiActivateFlags_None;frame();frame();
    };
    chooseFilter(lholo::i18n::TextKey::VerifierWrongType,MistakeFilter::WrongType,22);
    check(filterCalls==1 && model.schematic.filter==MistakeFilter::WrongType && !model.schematic.target,"category filter clears selection");
    selectRow(VerificationState::Extra,3);check(rowCalls==2,"hidden category cannot select through stale navigation ID");
    selectRow(VerificationState::WrongType,1);check(rowCalls==3,"filtered category row selects");
    chooseFilter(lholo::i18n::TextKey::SchematicFilterAll,MistakeFilter::Mistakes,90);
    selectRow(VerificationState::Missing,4);
    check(model.schematic.target->world==Cell{2,2,3},"aggregate row selects nearest original source index");
    check(model.verifierView->groups.front().indices==std::vector<std::size_t>({4,0}),"coordinate list is ordered nearest first");
    auto activateButton=[&](lholo::i18n::TextKey key){
        context->NavNextActivateId=ImGui::FindWindowByName("VerifierControlTest")->GetID(lholo::i18n::tr(key));
        context->NavNextActivateFlags=ImGuiActivateFlags_None;frame();
    };
    auto* details=windowContaining("##VerifierDetails");check(details && details->Active,"selected detail panel exists");
    context->NavNextActivateId=details->GetID(lholo::i18n::tr(lholo::i18n::TextKey::VerifierClearHighlight));
    frame();check(clearCalls==1 && !model.schematic.target,"clear uses existing highlight action");
    auto* window=ImGui::FindWindowByName("VerifierControlTest");
    auto searchId=window->GetID("##VerifierSearch");
    context->NavNextActivateId=searchId;context->NavNextActivateFlags=ImGuiActivateFlags_PreferInput;frame();
    check(context->ActiveId==searchId,"search receives keyboard focus");
    io.AddInputCharactersUTF8("BIRCH");frame();
    check(std::string_view(model.verifierView->search.data())=="BIRCH","search input persists");
    check(model.verifierView->categories[static_cast<int>(VerificationState::Missing)].empty(),"search hides nonmatching category");
    check(model.verifierView->categories[static_cast<int>(VerificationState::WrongType)].size()==1,"search matches actual block ignoring case");
    io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();
    check(context->ActiveId!=searchId,"escape releases search focus");
    auto const rebuilds=model.verifierView->rebuilds;for(int i=0;i<20;++i)frame();
    check(model.verifierView->rebuilds==rebuilds,"unchanged immutable results do not regroup every frame");
    model.verifierView->search.fill(0);frame();
    auto next=std::make_shared<schematic::Report>(*report);++next->stamp.loadedGeneration;
    model.verifierView->search[0]='x';model.schematic.report=next;frame();
    check(model.verifierView->search[0]==0,"reprojection clears old context search");
    model.schematic.report=report;frame();
    auto const rowsBeforeFilter=rowCalls;
    chooseFilter(lholo::i18n::TextKey::VerifierMissing,MistakeFilter::Missing,21);
    chooseFilter(lholo::i18n::TextKey::VerifierWrongState,MistakeFilter::WrongState,23);
    chooseFilter(lholo::i18n::TextKey::VerifierExtra,MistakeFilter::Extra,24);
    chooseFilter(lholo::i18n::TextKey::SchematicFilterAll,MistakeFilter::Mistakes,90);
    check(filterCalls==6 && rowCalls==rowsBeforeFilter && verifyCalls==0,
        "all four counted filters use whole-scan totals and never scan or select stale rows");
    model.schematic.activeProjectionAvailable=false;
    activateButton(lholo::i18n::TextKey::VerifierStart);check(verifyCalls==0,"start disabled without active selected projection");
    model.schematic.activeProjectionAvailable=true;
    activateButton(lholo::i18n::TextKey::VerifierStart);check(verifyCalls==1,"manual scan action");
    report->running=true;report->checked=256;report->progress=.25f;
    model.schematic.phase=schematic::VerificationPhase::Running;frame();
    activateButton(lholo::i18n::TextKey::VerifierStart);check(verifyCalls==1,"start disabled while running");
    activateButton(lholo::i18n::TextKey::VerifierCancel);check(cancelCalls==1,"running scan can stop");
    activateButton(lholo::i18n::TextKey::VerifierReset);check(resetCalls==1 && !model.schematic.report && !model.schematic.target,"reset discards result and selection");
    frame();check(model.verifierView->groups.empty(),"reset clears cached rows on the next snapshot");
    model.schematic.phase=schematic::VerificationPhase::Queued;frame();
    activateButton(lholo::i18n::TextKey::VerifierStart);check(verifyCalls==1,"queued scan cannot start twice");
    activateButton(lholo::i18n::TextKey::VerifierCancel);check(cancelCalls==2,"queued scan can stop");
    model.schematic.phase=schematic::VerificationPhase::Cancelled;frame();
    activateButton(lholo::i18n::TextKey::VerifierStart);check(verifyCalls==2,"stopped scan allows manual restart");
    activateButton(lholo::i18n::TextKey::VerifierCancel);check(cancelCalls==2,"stop disabled while idle");
    auto empty=std::make_shared<schematic::Report>();empty->stamp=report->stamp;empty->tally.correct=100;
    model.schematic.phase=schematic::VerificationPhase::Completed;model.schematic.report=empty;frame();frame();
    check(model.verifierView->groups.empty(),"empty result renders without phantom selection");
    auto large=std::make_shared<schematic::Report>(*empty);
    for(int i=0;i<50000;++i)large->mismatches.push_back({VerificationState::Missing,{i,64,0},"minecraft:block_"+std::to_string(i),"minecraft:air",double(i)});
    large->tally.missing=50000;large->truncated=true;model.schematic.report=large;frame();frame();
    check(model.verifierView->groups.size()==50000,"large fixture retains distinct real pairs");
    check(ImGui::GetDrawData()->TotalVtxCount<30000,"clipper bounds draw work for large results");
    auto const largeRebuilds=model.verifierView->rebuilds;frame();
    check(model.verifierView->rebuilds==largeRebuilds,"large unchanged report reuses aggregation cache");
    // Reordered equal properties must not appear as changes; missing properties do.
    auto differences=verifierStateChanges("minecraft:stairs [a=1, facing=south, b=true]",
        "minecraft:stairs [b=true, a=1, facing=north, extra=2]");
    check(differences.size()==2,"details list only changed or absent properties");
    check(differences[0].name=="extra" && differences[0].actual=="2" && !differences[0].expected,
        "actual-only state retains its exact value");
    check(differences[1].name=="facing" && differences[1].actual=="north" && differences[1].expected=="south",
        "detail changes show actual to expected");
    check(verifierStateChanges("minecraft:stone","minecraft:air").empty(),"stateless blocks have no invented changes");
    auto quoted=verifierStateChanges("minecraft:test [label=\"a,b\\\"c\", list=[1,2], empty=]",
        "minecraft:test [list=[1,2], label=\"a,b\\\"d\"]");
    check(quoted.size()==2 && quoted[0].name=="empty" && quoted[0].expected && !quoted[0].actual,
        "empty values remain distinct from absent properties");
    check(quoted[1].name=="label" && quoted[1].actual=="\"a,b\\\"d\"" && quoted[1].expected=="\"a,b\\\"c\"",
        "state details preserve quoted commas and escaped quotes");
    check(verifierMatchesSearch(report->mismatches[2],"OAK facing=north"),"search includes full actual states and all tokens");
    check(!verifierMatchesSearch(report->mismatches[2],"oak birch"),"all search tokens must match");
    check(verifierBlockParts(report->mismatches[2].expected).second=="[facing=south]","detail splits preserved native states");
    resetFluentTheme();ImGui::DestroyContext(context);
}

void testDirectMenuRoutes() {
    using namespace lholo::ui;
    using lholo::input::MenuRoute;
    auto* context=ImGui::CreateContext();context->ErrorCallback=errorCallback;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize={640,480};io.DeltaTime=1.f/60.f;
    ImFontConfig font;font.SizePixels=36;io.Fonts->AddFontDefault(&font);io.Fonts->Build();
    lholo::i18n::setLanguageByCode("en_US");
    MenuModel model;model.schematic.worldAvailable=true;model.schematic.session.writable=true;
    lholo::structure::SavedPlacement placement;placement.id=1;placement.name="Route test";placement.file="test.mcstructure";
    model.schematic.session.document.placements={placement};model.schematic.session.document.selected=1;
    model.schematic.files={"test.mcstructure"};
    model.hasLoadedStructure=true;model.materials={{"Stone",{},"minecraft:stone",64,64}};
    std::size_t verifyCalls{},placementCalls{};MenuActions actions;
    actions.verifySchematic=[&]{++verifyCalls;};actions.placeSchematic=[&](std::string const&){++placementCalls;};
    auto const metrics=calculateMetrics(io.DisplaySize,1);applyFluentTheme(metrics);
    auto frame=[&]{ImGui::NewFrame();renderMenu(model,actions,metrics);ImGui::Render();
        check(context->ErrorCountCurrentFrame==0,"direct route ImGui scopes");};
    auto pageWindow=[&]()->ImGuiWindow*{
        for(auto* window:context->Windows){auto name=std::string_view{window->Name};
            auto const separator=name.find_last_of('/');auto child=name.substr(separator==std::string_view::npos?0:separator+1);
            if(child.starts_with("##PageScroll"))return window;}
        return nullptr;
    };
    applyMenuRoutePresentation(model,MenuRoute::Placed);frame();frame();frame();
    check(model.page==MenuPage::Schematics,"placed route opens existing schematics page");
    auto* page=pageWindow();check(page && page->ScrollMax.y>0,"compact schematics page scrolls");
    check(page->Scroll.y==0,"cold placed route starts at its own placement section");
    bool placementSectionVisible{};
    for(auto* window:context->Windows)if(window->Active && std::string_view{window->Name}.find("##SchematicPlacements")!=std::string_view::npos)
        placementSectionVisible=window->Pos.y<page->ClipRect.Max.y;
    check(placementSectionVisible,"cold placed route shows placement controls without opening files first");
    applyMenuRoutePresentation(model,MenuRoute::Files);frame();frame();
    check(model.page==MenuPage::Projection && page->Scroll.y==0,"files route opens its dedicated file page at top");
    applyMenuRoutePresentation(model,MenuRoute::Verification);frame();frame();
    check(model.page==MenuPage::Verification,"verification route opens dedicated page");
    check(verifyCalls==0,"opening verification does not start a scan");
    applyMenuRoutePresentation(model,MenuRoute::Materials);frame();frame();
    check(model.page==MenuPage::Materials,"materials route opens its dedicated page");
    check(context->OpenPopupStack.Size==0,"materials page needs no intermediate popup");
    check(verifyCalls==0 && placementCalls==0,"navigation does not verify or place structures");
    check(model.directMenuRoute==MenuRoute::None,"route presentation is consumed once");
    resetFluentTheme();ImGui::DestroyContext(context);
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
    // Image/fallback geometry is exercised across every locale/viewport/scale.
    // IDs are draw-command fixtures; this suite does not submit GPU work.
    model.blockIcons=[](std::string_view block,std::string_view) {
        using namespace lholo::ui;
        auto name=verifierBlockParts(block).first;
        if(name=="minecraft:air")return BlockIconView{{},BlockIconStatus::Air};
        if(name=="minecraft:water")return BlockIconView{};
        return BlockIconView{123,BlockIconStatus::Ready,2.f};
    };
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
    model.comparisonStrength = static_cast<float>(state);
    model.correctionOutlineWidth = state == 0 ? 1.f : (state == 1 ? 4.f : 8.f);
    model.maxLayerX = 63;
    model.maxLayerY = 255;
    model.materialCount = 32;
    model.sizeX=64;model.sizeY=256;model.sizeZ=31;
    model.schematic.worldAvailable=state!=0;
    model.schematic.activeProjectionAvailable=state!=0;
    model.schematic.session.writable=state!=0;
    model.schematic.library="schematics/???";
    model.schematic.files={"a.mcstructure","folder/very-long-test-name.litematic"};
    lholo::structure::SavedPlacement p;p.id=1;p.file="a.mcstructure";p.name="Placement with a long name";
    model.schematic.session.document.placements={p};
    model.schematic.session.document.selected=state?1:0;
    if(state==2 || state==3){
        auto report=std::make_shared<lholo::structure::schematic::Report>();
        report->tally.correct=12;report->tally.missing=4;report->tally.wrongType=3;report->tally.wrongState=1;report->tally.extra=2;report->tally.unknown=7;report->truncated=true;
        report->stamp.worldEpoch=1;report->stamp.placementId=1;report->stamp.loadedGeneration=2;report->stamp.reportRevision=3;
        report->mismatches={
            {lholo::structure::VerificationState::Missing,{-27,65,34},"expected stairs [weirdo state]","actual air",100},
            {lholo::structure::VerificationState::WrongType,{-28,65,34},"minecraft:stone","minecraft:dirt",121},
            {lholo::structure::VerificationState::WrongState,{-29,65,34},"expected long asymmetric stair state south up","actual long asymmetric stair state north down",144},
            {lholo::structure::VerificationState::Extra,{-30,65,34},"minecraft:air","minecraft:stone",169}};
        report->materials={{"minecraft:slab",{24,8},12},{"minecraft:unknown_item",{3,0},{}}};
        model.schematic.report=report;model.schematic.target=report->mismatches[0];
        model.schematic.phase=state==3?lholo::structure::schematic::VerificationPhase::Running:lholo::structure::schematic::VerificationPhase::Completed;
        report->running=state==3;report->checked=123;report->progress=.4f;
    }
    if(state==4)model.schematic.phase=lholo::structure::schematic::VerificationPhase::Queued;
    if(state==5)model.schematic.phase=lholo::structure::schematic::VerificationPhase::Cancelled;

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
    model.directMenuRoutesReady=state!=4;
    if(state==3){model.hotkeys[13].conflict="Existing GUI hotkey with a long label";model.hotkeys[15].reserved=true;}
    auto const metrics = lholo::ui::calculateMetrics(viewport, scale);
    lholo::ui::applyFluentTheme(metrics);
    lholo::ui::MenuActions const actions{};
    for (std::size_t page = 0; page < lholo::ui::kMenuPageCount; ++page) {
        model.page = static_cast<lholo::ui::MenuPage>(page);
        model.schematic.filter=static_cast<lholo::structure::MistakeFilter>((page+static_cast<std::size_t>(state))%6);
        model.layerAxis = static_cast<int>(page % 9);
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
        testComparisonControls();
        testCompactNavigation();
        testVerifierControls();
        testDirectMenuRoutes();
        for (std::size_t language = 0; language < lholo::i18n::languages().size(); ++language) {
            check(lholo::i18n::setLanguageByCode(lholo::i18n::languageCode(language)), "language selection");
            for (auto const viewport : {ImVec2{1920, 1080}, ImVec2{3840, 2160},
                    ImVec2{640, 480}, ImVec2{480, 800}}) {
                for (float scale : {1.f, 2.f, 5.f}) {
                    for (int state = 0; state < 6; ++state) renderPages(viewport, scale, state, static_cast<int>(language));
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
