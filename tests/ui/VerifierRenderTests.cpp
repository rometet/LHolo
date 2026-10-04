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
void renderCase(std::filesystem::path const& output,int width,int height,float scale,bool running,bool scroll=false,int directCase=0) {
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
        {VerificationState::WrongState,{143,65,-12},"minecraft:oak_stairs [upside_down_bit=false, weirdo_direction=2]","minecraft:oak_stairs [upside_down_bit=true, weirdo_direction=0]",36},
        {VerificationState::Extra,{144,65,-12},"minecraft:air","minecraft:dirt",49}};
    model.schematic.target=report->mismatches[0];model.schematic.report=report;
    model.schematic.phase=running?schematic::VerificationPhase::Running:schematic::VerificationPhase::Completed;
    report->running=running;report->checked=17200;report->progress=.42f;
    if(directCase==4)report->materials={{"minecraft:stone_bricks",{128,96},160},{"minecraft:oak_stairs",{32,24},16}};
    auto const metrics=calculateMetrics(io.DisplaySize,scale);applyFluentTheme(metrics);
    for(int frame=0;frame<3;++frame){ImGui::NewFrame();renderMenu(model,{},metrics);ImGui::Render();require(context->ErrorCountCurrentFrame==0,"ImGui diagnostics");require(io.Fonts->TexWidth==atlasWidth && io.Fonts->TexHeight==atlasHeight,"Japanese UI does not resize/reload the atlas");}
    if(scroll){
        ImGuiWindow* page{};
        for(auto* window:context->Windows){
            auto const name=std::string_view(window->Name);
            auto const separator=name.find_last_of('/');
            auto const child=name.substr(separator==std::string_view::npos?0:separator+1);
            if(child.starts_with("##PageScroll"))page=window;
        }
        require(page && page->ScrollMax.y>0,"compact verifier has page scroll");
        ImGui::SetScrollY(page,page->ScrollMax.y);
        for(int frame=0;frame<2;++frame){ImGui::NewFrame();renderMenu(model,{},metrics);ImGui::Render();}
        require(page->Scroll.y>0,"compact verifier scroll reaches lower content");
    }
    saveRender(output,width,height);
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
        renderCase(dir/"direct-hotkeys-1920.ppm",1920,1080,1,false,true,1);
        renderCase(dir/"direct-hotkeys-640.ppm",640,480,1,false,true,2);
        renderCase(dir/"direct-placed-640.ppm",640,480,1,false,false,3);
        renderCase(dir/"direct-materials-1920.ppm",1920,1080,1,false,false,4);
        renderCase(dir/"direct-placed-1920.ppm",1920,1080,1,false,false,5);
        renderCase(dir/"direct-files-1920.ppm",1920,1080,1,false,false,6);
        for(std::size_t page=0;page<lholo::ui::kMenuPageCount;++page) {
            renderCase(dir/("japanese-page-"+std::to_string(page)+"-1280.ppm"),1280,720,1,false,false,100+int(page));
        }
        return 0;
    }catch(std::exception const& e){std::fprintf(stderr,"Verifier render failed: %s\n",e.what());return 1;}
}
