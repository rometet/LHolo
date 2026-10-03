#include "ui/LHoloMenu.h"
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
void renderCase(std::filesystem::path const& output,int width,int height,float scale,bool running,bool scroll=false) {
    using namespace lholo::ui;using namespace lholo::structure;
    auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={static_cast<float>(width),static_cast<float>(height)};io.DeltaTime=1.f/60.f;
    lholo::overlay::loadOverlayFonts(*io.Fonts,{"C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/meiryo.ttc","C:/Windows/Fonts/seguisym.ttf"});
    require(io.Fonts->Build(),"actual Windows font atlas");
    require(io.Fonts->Fonts[0]->FindGlyphNoFallback(0x691c)!=nullptr,"Japanese verification glyph");
    require(lholo::i18n::setLanguageByCode("ja_JP"),"Japanese UI");
    MenuModel model;model.page=MenuPage::Verification;
    model.schematic.worldAvailable=true;model.schematic.session.writable=true;
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
    auto const metrics=calculateMetrics(io.DisplaySize,scale);applyFluentTheme(metrics);
    for(int frame=0;frame<3;++frame){ImGui::NewFrame();renderMenu(model,{},metrics);ImGui::Render();require(context->ErrorCountCurrentFrame==0,"ImGui diagnostics");}
    if(scroll){
        ImGuiWindow* page{};
        for(auto* window:context->Windows)if(std::string_view(window->Name).find("##PageScroll")!=std::string_view::npos)page=window;
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
        return 0;
    }catch(std::exception const& e){std::fprintf(stderr,"Verifier render failed: %s\n",e.what());return 1;}
}
