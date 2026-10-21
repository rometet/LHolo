#include "ui/LHoloMenu.h"
#include "ui/MenuRoutePresentation.h"
#include "i18n/LanguageStore.h"
#include "overlay/OverlayFonts.h"
#include <backends/imgui_impl_dx11.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>
#include "JapaneseUiText.inc"

namespace {
using Microsoft::WRL::ComPtr;
void require(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
void saveRender(std::filesystem::path const& path,int width,int height) {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;
    desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;
    require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture)),"render texture");
    require(SUCCEEDED(device->CreateRenderTargetView(texture.Get(),nullptr,&target)),"render view");
    auto* view=target.Get();context->OMSetRenderTargets(1,&view,nullptr);
    float const background[]{.09f,.1f,.12f,1.f};context->ClearRenderTargetView(view,background);
    auto const previous=ImGui::GetIO().Fonts->TexID;
    require(ImGui_ImplDX11_Init(device.Get(),context.Get()),"DX11 backend");
    require(ImGui_ImplDX11_CreateDeviceObjects(),"font GPU texture");
    auto* data=ImGui::GetDrawData();
    require(data && data->Valid && data->TotalVtxCount>0,"menu draw data");
    for(auto* list:data->CmdLists)for(auto& command:list->CmdBuffer)
        if(command.TextureId==previous)command.TextureId=ImGui::GetIO().Fonts->TexID;
    // Diagnostic textures are synthetic patterns, not Minecraft artwork.
    // Rebind fixture image IDs to owned resources on this WARP device.
    std::vector<ComPtr<ID3D11ShaderResourceView>> iconViews;
    for(unsigned i=0;i<8;++i) {
        std::array<unsigned char,16*16*4> pixels{};
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
            auto const at=(y*16+x)*4;bool const light=((x/4+y/4)%2)==0;
            pixels[at]=static_cast<unsigned char>(light?70+i*20:30+i*12);
            pixels[at+1]=static_cast<unsigned char>(light?170-i*12:70-i*5);
            pixels[at+2]=static_cast<unsigned char>(light?210-i*16:100-i*8);pixels[at+3]=255;
        }
        D3D11_TEXTURE2D_DESC icon{};icon.Width=icon.Height=16;icon.MipLevels=icon.ArraySize=1;
        icon.Format=DXGI_FORMAT_R8G8B8A8_UNORM;icon.SampleDesc.Count=1;icon.Usage=D3D11_USAGE_IMMUTABLE;icon.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA source{pixels.data(),64,0};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
        require(SUCCEEDED(device->CreateTexture2D(&icon,&source,&texture)),"fixture icon texture");
        require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&view)),"fixture icon SRV");iconViews.push_back(view);
    }
    for(auto* list:data->CmdLists)for(auto& command:list->CmdBuffer)
        if(command.TextureId>=0xB10C0000 && command.TextureId<0xB10C0008)
            command.TextureId=reinterpret_cast<ImTextureID>(iconViews[static_cast<std::size_t>(command.TextureId-0xB10C0000)].Get());
    ImGui_ImplDX11_RenderDrawData(data);
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)),"readback texture");
    context->CopyResource(readback.Get(),texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"readback");
    std::ofstream output(path,std::ios::binary);output<<"P6\n"<<width<<' '<<height<<"\n255\n";
    for(int y=0;y<height;++y){auto const* row=static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch;
        for(int x=0;x<width;++x)output.write(reinterpret_cast<char const*>(row+x*4),3);}
    context->Unmap(readback.Get(),0);ImGui_ImplDX11_Shutdown();
    require(output.good(),"render artifact write");
}
void renderCase(std::filesystem::path const& output,int width,int height,float scale,bool running,bool scroll=false,int directCase=0,bool iconFixture=false) {
    using namespace lholo::ui;using namespace lholo::structure;
    auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={static_cast<float>(width),static_cast<float>(height)};io.DeltaTime=1.f/60.f;
    lholo::overlay::loadOverlayFonts(*io.Fonts,{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/meiryo.ttc","C:/Windows/Fonts/seguisym.ttf"});
    require(io.Fonts->Build(),"actual Windows font atlas");
    require(io.Fonts->Fonts[0]->FindGlyphNoFallback(0x691c)!=nullptr,"Japanese verification glyph");
    require(lholo::i18n::setLanguageByCode("ja_JP"),"Japanese UI");
    auto const atlasWidth=io.Fonts->TexWidth,atlasHeight=io.Fonts->TexHeight;
    require(atlasWidth==8192 && atlasHeight<=16384,"existing atlas dimensions stay within the accepted budget");
    for(std::size_t key=1;key<lholo::i18n::kTextKeyCount;++key) {
        auto* text=lholo::i18n::tr(static_cast<lholo::i18n::TextKey>(key));
        while(*text) {
            unsigned int character{};int const bytes=ImTextCharFromUtf8(&character,text,nullptr);
            require(bytes>0,"valid UTF-8 Japanese catalog");text+=bytes;
            if(character<32)continue;
            if(!io.Fonts->Fonts[0]->FindGlyphNoFallback(static_cast<ImWchar>(character))) {
                std::fprintf(stderr,"Missing LHolo Japanese glyph U+%04X key=%zu\n",character,key);
                require(false,"Japanese catalog uses the existing actual font glyphs");
            }
        }
    }
    std::printf("Japanese LHolo atlas %dx%d fonts=%d sources=%d\n",atlasWidth,atlasHeight,io.Fonts->Fonts.Size,io.Fonts->Sources.Size);
    for(auto* text:japaneseDisplayCorpus)while(*text) {
        unsigned int character{};int const bytes=ImTextCharFromUtf8(&character,text,nullptr);
        require(bytes>0,"valid UTF-8 in Japanese API/status display");text+=bytes;
        if(character<32)continue;
        require(io.Fonts->Fonts[0]->FindGlyphNoFallback(static_cast<ImWchar>(character))!=nullptr,
                "Japanese API/status text uses existing actual font glyphs");
    }
    MenuModel model;model.page=MenuPage::Verification;
    std::size_t iconLookups{};
    if(iconFixture)model.blockIcons=[&](std::string_view block,std::string_view) {
        ++iconLookups;
        auto const name=verifierBlockParts(block).first;
        if(name=="minecraft:air")return BlockIconView{{},BlockIconStatus::Air};
        if(name=="minecraft:birch_planks" || name=="minecraft:water")return BlockIconView{};
        std::size_t hash{};for(auto c:name)hash=hash*31+static_cast<unsigned char>(c);
        return BlockIconView{0xB10C0000+(hash%8),BlockIconStatus::Ready};
    };
    if(directCase>=30 && directCase<40) {
        using namespace lholo::structure::detail;
        model.page=MenuPage::Materials;model.hasLoadedStructure=true;
        auto bill=std::make_shared<MaterialListSnapshot>();bill->scope={71,9};bill->source="fixture/材料一覧.mcstructure";bill->revision=5;
        bill->requirements={{"石レンガ",{},"minecraft:stone_bricks","minecraft:stone_bricks",128,64,"body:item:stone_bricks"},
            {"オークの階段",{},"minecraft:oak_stairs","minecraft:oak_stairs",32,64,"body:item:oak_stairs"},
            {"ガラス",{},"minecraft:glass","minecraft:glass",64,64,"body:item:glass"},
            {"水",{},"minecraft:water",{},24,64,"liquid:water"}};
        bill->available={64,48,16,std::nullopt};bill->ignored.insert("body:item:glass");
        model.materialList=bill;model.materialsView=std::make_shared<MaterialsViewState>();
        if(directCase==31)model.materialsView->filter=MaterialFilter::Shortage;
        if(directCase==32)model.materialsView->filter=MaterialFilter::Ignored;
        if(directCase==33){bill->ignored.clear();model.materialsView->filter=MaterialFilter::Ignored;}
        if(directCase==34)model.materialExport.phase=lholo::io::MaterialExportPhase::Saving;
        if(directCase==35){model.materialExport={lholo::io::MaterialExportPhase::Failed,L"C:/fixture/保存先/materials.tsv","Access denied (fixture)",{71,9}};}
        if(directCase==38){model.materialExport={lholo::io::MaterialExportPhase::Saved,L"C:/fixture/日本語フォルダー/材料一覧.tsv",{},{71,9},bill->source};}
        if(directCase==37){
            bill->requirements.clear();bill->available.clear();bill->ignored.clear();
            for(int i=0;i<50000;++i){bill->requirements.push_back({"材料 "+std::to_string(i),{},"minecraft:block_"+std::to_string(i),"item",64,64,"key_"+std::to_string(i)});bill->available.push_back(32);}
        }
    }
    if(directCase>=100) {
        model.page=static_cast<MenuPage>(directCase-100);model.hasLoadedStructure=true;
        model.captureWorldAvailable=true;model.capture.first={true,10,64,10};model.capture.second={true,20,70,20};
        model.pathBuffer=nullptr;model.pathBufferSize=0;
    }
    if(directCase==1 || directCase==2 || model.page==MenuPage::Hotkeys) {
        model.page=MenuPage::Hotkeys;
        std::array<lholo::i18n::TextKey,16> labels{
            lholo::i18n::TextKey::HotkeyOpenMenu,lholo::i18n::TextKey::HotkeyMoveLeft,lholo::i18n::TextKey::HotkeyMoveRight,
            lholo::i18n::TextKey::HotkeyMoveForward,lholo::i18n::TextKey::HotkeyMoveBackward,lholo::i18n::TextKey::HotkeyMoveUp,
            lholo::i18n::TextKey::HotkeyMoveDown,lholo::i18n::TextKey::HotkeyLayerIncrease,lholo::i18n::TextKey::HotkeyLayerDecrease,
            lholo::i18n::TextKey::HotkeyLoadProjection,lholo::i18n::TextKey::HotkeyCloseProjection,lholo::i18n::TextKey::CheckboxManualPlace,
            lholo::i18n::TextKey::HotkeyOpenPlaced,lholo::i18n::TextKey::HotkeyOpenFiles,
            lholo::i18n::TextKey::HotkeyOpenVerification,lholo::i18n::TextKey::HotkeyOpenMaterials};
        for(std::size_t i=0;i<model.hotkeys.size();++i)
            model.hotkeys[i]={static_cast<HotkeyId>(i),lholo::i18n::tr(labels[i]),i>=12?"---":"F10",false};
        model.hotkeys[13].conflict=lholo::i18n::tr(labels[0]);model.hotkeys[15].reserved=true;
        model.directMenuRoutesReady=directCase==1;
    }
    if(directCase==3 || directCase==5)applyMenuRoutePresentation(model,lholo::input::MenuRoute::Placed);
    if(directCase==4)applyMenuRoutePresentation(model,lholo::input::MenuRoute::Materials);
    if(directCase==6)applyMenuRoutePresentation(model,lholo::input::MenuRoute::Files);
    if(directCase==4) {
        model.hasLoadedStructure=true;
        model.materials={{"Stone bricks",{},"minecraft:stone_bricks",128,64},{"Oak stairs",{},"minecraft:oak_stairs",32,64}};
    }
    model.schematic.worldAvailable=true;model.schematic.session.writable=true;
    model.schematic.files={"建築サンプル.mcstructure","建築サンプル.litematic"};
    model.schematic.activeProjectionAvailable=true;
    SavedPlacement p;p.id=1;p.name="検証サンプル / 共有フォント・テーマ";p.file="sample.mcstructure";
    model.schematic.session.document.placements={p};model.schematic.session.document.selected=1;
    auto report=std::make_shared<schematic::Report>();report->stamp.worldEpoch=1;report->stamp.placementId=1;report->stamp.loadedGeneration=1;report->stamp.reportRevision=1;
    report->tally.correct=240;report->tally.missing=2;report->tally.wrongType=1;report->tally.wrongState=1;report->tally.extra=1;report->tally.unknown=2;
    report->mismatches={
        {VerificationState::Missing,{140,65,-12},"minecraft:stone_bricks","minecraft:air",9},
        {VerificationState::Missing,{141,65,-12},"minecraft:stone_bricks","minecraft:air",16},
        {VerificationState::WrongType,{142,65,-12},"minecraft:oak_planks","minecraft:birch_planks",25},
        {VerificationState::WrongState,{143,65,-12},"minecraft:oak_stairs [upside_down_bit=0b, weirdo_direction=2]","minecraft:oak_stairs [upside_down_bit=1b, weirdo_direction=0]",36},
        {VerificationState::Extra,{144,65,-12},"minecraft:air","minecraft:dirt",49}};
    model.schematic.target=report->mismatches[0];model.schematic.report=report;
    model.schematic.phase=running?schematic::VerificationPhase::Running:schematic::VerificationPhase::Completed;
    report->running=running;report->checked=17200;report->progress=.42f;
    if(directCase==4)report->materials={{"minecraft:stone_bricks",{128,96},160},{"minecraft:oak_stairs",{32,24},16}};
    if(directCase==10){report->mismatches.clear();report->tally={};report->tally.correct=4096;model.schematic.target.reset();}
    if(directCase==11 || directCase==18){
        report->mismatches.clear();report->tally={};report->tally.missing=200000;report->truncated=true;
        for(int i=0;i<1536;++i)report->mismatches.push_back({VerificationState::Missing,{140+i,65,-12},
            directCase==18?"minecraft:stone_bricks":"minecraft:block_"+std::to_string(i),"minecraft:air",double(i*i)});
        model.schematic.target=report->mismatches[0];
    }
    if(directCase==12){model.schematic.phase=schematic::VerificationPhase::Queued;model.schematic.report.reset();model.schematic.target.reset();}
    if(directCase==13){model.schematic.phase=schematic::VerificationPhase::Cancelled;model.schematic.report.reset();model.schematic.target.reset();}
    if(directCase==14){model.schematic.phase=schematic::VerificationPhase::NotVerified;model.schematic.report.reset();model.schematic.target.reset();}
    model.verifierView=std::make_shared<VerifierViewState>();
    if(directCase==15)std::snprintf(model.verifierView->search.data(),model.verifierView->search.size(),"oak upside_down_bit");
    if(directCase==16)model.schematic.target=report->mismatches[3];
    if(directCase==17)model.verifierView->errorsOnly=true;
    if(directCase==19)model.schematic.target.reset();
    if(directCase==39){model.materialExport.phase=lholo::io::MaterialExportPhase::Saving;model.materialExport.accepting=false;}
    if(directCase==42)model.schematic.activeProjectionAvailable=false;
    std::size_t verifyCalls{},cancelCalls{},resetCalls{};
    MenuActions actions;
    if(directCase>=30 && directCase<40) {
        actions.requestMaterials=[]{};
        actions.ignoreMaterial=[](auto,auto const&,bool){};
        actions.clearIgnoredMaterials=[](auto){};
        actions.exportMaterials=[](auto){}; // Image fixture never invokes a native save or writes world data.
    }
    actions.verifySchematic=[&]{++verifyCalls;model.schematic.phase=schematic::VerificationPhase::Queued;model.schematic.report.reset();};
    actions.cancelVerification=[&]{++cancelCalls;model.schematic.phase=schematic::VerificationPhase::Cancelled;model.schematic.report.reset();};
    actions.resetVerification=[&]{++resetCalls;model.schematic.phase=schematic::VerificationPhase::NotVerified;model.schematic.report.reset();model.schematic.target.reset();};
    actions.setMistakeFilter=[&](MistakeFilter filter){model.schematic.filter=filter;model.schematic.target.reset();};
    actions.selectMistake=[&](auto const& stamp,auto index){
        schematic::MistakeSelection selection;selection.setFilter(model.schematic.filter);
        if(selection.select(stamp,report->stamp,report->running,report->mismatches,index))model.schematic.target=report->mismatches[index];
    };
    actions.clearMistakeTarget=[&]{model.schematic.target.reset();};
    actions.cycleMistake=[&](MistakeFilter filter){
        schematic::MistakeSelection selection;
        if(selection.cycle(report->stamp,report->running,report->mismatches,filter))model.schematic.target=selection.target(report->stamp)->mismatch;
    };
    auto const metrics=calculateMetrics(io.DisplaySize,scale);applyFluentTheme(metrics);
    for(int frame=0;frame<3;++frame){ImGui::NewFrame();renderMenu(model,actions,metrics);ImGui::Render();require(context->ErrorCountCurrentFrame==0,"ImGui diagnostics");require(io.Fonts->TexWidth==atlasWidth && io.Fonts->TexHeight==atlasHeight,"Japanese UI does not resize/reload the atlas");}
    auto windowWithChild=[&](char const* child){
        for(auto* window:context->Windows){auto const name=std::string_view(window->Name);auto const at=name.find_last_of('/');
            if(name.substr(at==name.npos?0:at+1).starts_with(child))return window;}
        return static_cast<ImGuiWindow*>(nullptr);
    };
    auto* toolbar=windowWithChild("##VerificationToolbar");
    auto const toolbarPos=toolbar?toolbar->Pos:ImVec2{};
    if(scroll){
        ImGuiWindow* page{};
        for(auto* window:context->Windows){
            auto const name=std::string_view(window->Name);
            auto const separator=name.find_last_of('/');
            auto const child=name.substr(separator==std::string_view::npos?0:separator+1);
            if(child.starts_with("##PageScroll"))page=window;
        }
        require(page && page->ScrollMax.y>0,"compact verifier has page scroll");
        auto const target=directCase==39?180.f:page->ScrollMax.y;
        ImGui::SetScrollY(page,target);
        for(int frame=0;frame<4;++frame){ImGui::SetScrollY(page,directCase==39?target:page->ScrollMax.y);ImGui::NewFrame();renderMenu(model,actions,metrics);ImGui::Render();}
        require(page->Scroll.y>0,"compact verifier scroll reaches lower content");
        require(std::abs(page->Scroll.y-(directCase==39?target:page->ScrollMax.y))<1.f,"compact page reaches actual scroll target after auto-size settles");
        if(model.page==MenuPage::Verification) {
            require(toolbar && toolbar->Pos.x==toolbarPos.x && toolbar->Pos.y==toolbarPos.y,
                "manual controls keep their screen position when body scrolls");
        }
    }
    if(directCase==36){
        ImGuiWindow* page{};ImGuiWindow* panel{};
        for(auto* window:context->Windows){auto name=std::string_view(window->Name);auto const separator=name.find_last_of('/');auto const child=name.substr(separator==std::string_view::npos?0:separator+1);
            if(child.starts_with("##PageScroll"))page=window;
            if(child.starts_with("##Materials_") && name.find("MaterialsWorkTable")==std::string_view::npos)panel=window;}
        require(page && panel,"compact materials card exists");ImGui::SetScrollY(page,panel->Pos.y-page->Pos.y+page->Scroll.y);
        for(int frame=0;frame<3;++frame){ImGui::NewFrame();renderMenu(model,actions,metrics);ImGui::Render();}
        require(panel->Pos.y<page->ClipRect.Max.y,"compact material table reachable");
    }
    if(directCase==20 || directCase==21){
        ImGuiWindow* page{};ImGuiWindow* panel{};
        for(auto* window:context->Windows){
            auto name=std::string_view(window->Name);auto const separator=name.find_last_of('/');
            auto child=name.substr(separator==std::string_view::npos?0:separator+1);
            if(child.starts_with("##PageScroll"))page=window;
            if(child.starts_with(directCase==20?"##MismatchList":"##VerifierDetails"))panel=window;
        }
        require(page && panel,"compact list/detail panel exists");
        ImGui::SetScrollY(page,panel->Pos.y-page->Pos.y+page->Scroll.y);
        for(int frame=0;frame<2;++frame){ImGui::NewFrame();renderMenu(model,actions,metrics);ImGui::Render();}
        require(panel->Pos.y < page->ClipRect.Max.y && panel->Pos.y >= page->ClipRect.Min.y-2,"compact panel is reachable at page scroll target");
    }
    if(iconFixture) {
        std::size_t images{};for(auto* list:ImGui::GetDrawData()->CmdLists)for(auto const& command:list->CmdBuffer)
            if(command.TextureId>=0xB10C0000 && command.TextureId<0xB10C0008)++images;
        require(images>0,"visible icon fixture produces image commands");
        require(iconLookups<1000,"icon lookup remains limited to visible rows");
    }
    saveRender(output,width,height);
    require(verifyCalls==0 && cancelCalls==0 && resetCalls==0,"opening and scrolling a page never invokes scan controls");
    if(directCase>=40 && directCase<=43) {
        require(toolbar && toolbar->Active,"pinned toolbar remains active");
        auto const widthFor=[](lholo::i18n::TextKey key){return ImGui::CalcTextSize(lholo::i18n::tr(key)).x+ImGui::GetStyle().FramePadding.x*2;};
        auto const startWidth=widthFor(lholo::i18n::TextKey::VerifierStart);
        auto const cancelWidth=widthFor(lholo::i18n::TextKey::VerifierCancel);
        auto point=toolbar->DC.CursorStartPos;
        point.y+=ImGui::GetFrameHeight()*.5f;
        if(directCase==41)point.x+=startWidth+metrics.gap+cancelWidth*.5f;
        else if(directCase==43)point.x+=startWidth+cancelWidth+metrics.gap*2+widthFor(lholo::i18n::TextKey::VerifierReset)*.5f;
        else point.x+=startWidth*.5f;
        require(toolbar->ClipRect.Contains(point),"manual control is within visible toolbar bounds");
        io.AddMousePosEvent(point.x,point.y);
        for(int frame=0;frame<2;++frame){ImGui::NewFrame();renderMenu(model,actions,metrics);ImGui::Render();}
        io.AddMouseButtonEvent(0,true);ImGui::NewFrame();renderMenu(model,actions,metrics);ImGui::Render();
        io.AddMouseButtonEvent(0,false);ImGui::NewFrame();renderMenu(model,actions,metrics);ImGui::Render();
        require(context->ErrorCountCurrentFrame==0,"mouse activation keeps UI scopes balanced");
        require(verifyCalls==(directCase==40?1:0) && cancelCalls==(directCase==41?1:0) && resetCalls==(directCase==43?1:0),
            "visible manual control forwards exactly one authorized action, disabled start forwards none");
    }
    std::printf("Verifier render: %dx%d scale=%.1f running=%d PASS\n",width,height,scale,running);
    resetFluentTheme();ImGui::DestroyContext(context);
}
}
int main(int argc,char** argv) {
    try {
        lholo::i18n::initLanguageStore();
        auto const dir=std::filesystem::path(argc>1?argv[1]:"build/verifier-preview");std::filesystem::create_directories(dir);
        renderCase(dir/"verifier-1920.ppm",1920,1080,1,false);
        renderCase(dir/"verifier-3840.ppm",3840,2160,2,false);
        renderCase(dir/"verifier-640.ppm",640,480,1,false);
        renderCase(dir/"verifier-640-scrolled.ppm",640,480,1,false,true);
        renderCase(dir/"verifier-progress.ppm",1920,1080,1,true);
        renderCase(dir/"verifier-empty.ppm",1920,1080,1,false,false,10);
        renderCase(dir/"verifier-large.ppm",1920,1080,1,false,false,11);
        renderCase(dir/"verifier-queued.ppm",1920,1080,1,false,false,12);
        renderCase(dir/"verifier-cancelled.ppm",1920,1080,1,false,false,13);
        renderCase(dir/"verifier-not-verified.ppm",1920,1080,1,false,false,14);
        renderCase(dir/"verifier-search.ppm",1920,1080,1,false,false,15);
        renderCase(dir/"verifier-state-detail.ppm",1920,1080,1,false,false,16);
        renderCase(dir/"verifier-errors-only.ppm",1920,1080,1,false,false,17);
        renderCase(dir/"verifier-many-coordinates.ppm",1920,1080,1,false,false,18);
        renderCase(dir/"verifier-no-selection.ppm",1920,1080,1,false,false,19);
        renderCase(dir/"verifier-640-list.ppm",640,480,1,false,false,20);
        renderCase(dir/"verifier-640-detail.ppm",640,480,1,false,false,21);
        renderCase(dir/"verifier-pinned-start-640.ppm",640,480,1,false,true,40);
        renderCase(dir/"verifier-pinned-cancel-640.ppm",640,480,1,true,false,41);
        renderCase(dir/"verifier-disabled-start-640.ppm",640,480,1,false,true,42);
        renderCase(dir/"verifier-pinned-reset-1920.ppm",1920,1080,1,false,false,43);
        renderCase(dir/"verifier-pinned-start-1280-scale2.ppm",1280,720,2,false,true,40);
        renderCase(dir/"direct-hotkeys-1920.ppm",1920,1080,1,false,true,1);
        renderCase(dir/"direct-hotkeys-640.ppm",640,480,1,false,true,2);
        renderCase(dir/"direct-placed-640.ppm",640,480,1,false,false,3);
        renderCase(dir/"direct-materials-1920.ppm",1920,1080,1,false,false,4);
        renderCase(dir/"direct-placed-1920.ppm",1920,1080,1,false,false,5);
        renderCase(dir/"direct-files-1920.ppm",1920,1080,1,false,false,6);
        for(std::size_t page=0;page<lholo::ui::kMenuPageCount;++page) {
            renderCase(dir/("japanese-page-"+std::to_string(page)+"-1280.ppm"),1280,720,1,false,false,100+int(page));
        }
        renderCase(dir/"materials-all-1920.ppm",1920,1080,1,false,false,30);
        renderCase(dir/"materials-shortage.ppm",1920,1080,1,false,false,31);
        renderCase(dir/"materials-ignored.ppm",1920,1080,1,false,false,32);
        renderCase(dir/"materials-empty.ppm",1920,1080,1,false,false,33);
        renderCase(dir/"materials-saving.ppm",1920,1080,1,false,false,34);
        renderCase(dir/"materials-failed.ppm",1920,1080,1,false,false,35);
        renderCase(dir/"materials-640-controls.ppm",640,480,1,false,false,30);
        renderCase(dir/"materials-640-table.ppm",640,480,1,false,false,36);
        renderCase(dir/"materials-large.ppm",1920,1080,1,false,false,37);
        renderCase(dir/"materials-saved.ppm",1920,1080,1,false,false,38);
        renderCase(dir/"materials-3840.ppm",3840,2160,2,false,false,30);
        renderCase(dir/"materials-stopping-1920.ppm",1920,1080,1,false,false,39);
        renderCase(dir/"materials-stopping-640.ppm",640,480,1,false,false,39);
        renderCase(dir/"materials-stopping-640-controls.ppm",640,480,1,false,true,39);
        renderCase(dir/"icons-verifier-1920-synthetic.ppm",1920,1080,1,false,false,0,true);
        renderCase(dir/"icons-verifier-640-list-synthetic.ppm",640,480,1,false,false,20,true);
        renderCase(dir/"icons-verifier-640-detail-synthetic.ppm",640,480,1,false,false,21,true);
        renderCase(dir/"icons-verifier-3840-synthetic.ppm",3840,2160,2,false,false,16,true);
        renderCase(dir/"icons-materials-1920-synthetic.ppm",1920,1080,1,false,false,30,true);
        renderCase(dir/"icons-materials-640-synthetic.ppm",640,480,1,false,false,36,true);
        renderCase(dir/"icons-materials-3840-synthetic.ppm",3840,2160,2,false,false,30,true);
        return 0;
    }catch(std::exception const& e){std::fprintf(stderr,"Verifier render failed: %s\n",e.what());return 1;}
}
