#pragma once
#include "ui/BlockIcon.h"
#include "ui/BlockIconStore.h"
#include "resourcepacks/WorldPackReloadState.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <map>
#include <vector>

namespace lholo::overlay {
// Present/context-lock confined. Only LHolo-created resources live here.
// Pin every image referenced by draw commands until RenderDrawData returns,
// even when the cache generation changes or an entry is evicted mid-frame.
class BlockIconGpu {
    struct Entry {
        std::uint64_t generation{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    };
    ID3D11Device* device{}; // borrowed only between beginFrame/endFrame
    ID3D11Device* cacheDevice{}; // identity only; cached SRVs keep their device alive
    std::map<ui::icons::Key,Entry> cache;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> frame;
public:
    void beginFrame(ID3D11Device* current) {
        if(resourcepacks::state().snapshot()->closed) {
            if(cacheDevice && cacheDevice!=current)resourcepacks::state().invalidate();
            device=nullptr;return;
        }
        frame.clear();
        if(cacheDevice!=current){cache.clear();cacheDevice=current;}
        device=current;
    }
    void endFrame() {if(!resourcepacks::state().snapshot()->closed)frame.clear();device=nullptr;}
    void reset() {
        if(resourcepacks::state().snapshot()->closed) {
            resourcepacks::state().invalidate();device=nullptr;return;
        }
        frame.clear();cache.clear();device=nullptr;cacheDevice=nullptr;
    }
    std::size_t cached() const {return cache.size();}
    std::size_t pinned() const {return frame.size();}
    ui::BlockIconView lookup(std::string_view block,std::string_view item) {
        auto lease=resourcepacks::state().enter(resourcepacks::Role::IconRender);
        if(!lease)return {};
        auto result=lookupOwned(block,item);
        if(result.status==ui::BlockIconStatus::Ready)
            resourcepacks::state().submitted(resourcepacks::Domain::Icons);
        return result;
    }
    // Only the existing Present owner calls these after the old event query
    // completed. No engine handles or shared ImGui/backend owners are retired.
    std::vector<ui::icons::Key> retireForReload() {
        std::vector<ui::icons::Key> keys;keys.reserve(cache.size());
        for(auto const& [key,entry]:cache)keys.push_back(key);
        frame.clear();cache.clear();device=nullptr;cacheDevice=nullptr;return keys;
    }
    bool rebuildViews(ID3D11Device* current,std::vector<ui::icons::Key> const& keys) {
        auto lease=resourcepacks::state().enter(resourcepacks::Role::IconRender,true);
        if(!lease || !current)return false;
        if(cacheDevice && cacheDevice!=current){resourcepacks::state().invalidate();return false;}
        cacheDevice=current;device=current;
        if(!keys.empty())resourcepacks::state().submitted(resourcepacks::Domain::Icons);
        struct ClearBorrow {ID3D11Device*& value;~ClearBorrow(){value=nullptr;}} clear{device};
        for(auto const& key:keys) {
            auto result=lookupOwned(key.block,key.item);
            if(result.status==ui::BlockIconStatus::Pending)return false;
            auto pixels=ui::icons::store().request(key).pixels;
            if(pixels && result.status!=ui::BlockIconStatus::Ready)return false;
        }
        frame.clear();return resourcepacks::state().mayPublish(lease);
    }
private:
    ui::BlockIconView lookupOwned(std::string_view block,std::string_view item) {
        using namespace ui::icons;
        auto const key=keyFor(block,item);
        if(air(key))return {{},ui::BlockIconStatus::Air};
        auto const result=store().request(key);
        if(!device || !result.pixels)return {{},result.pending ? ui::BlockIconStatus::Pending : ui::BlockIconStatus::Unavailable};
        auto found=cache.find(key);
        if(found==cache.end() || found->second.generation!=result.generation) {
            auto const& pixels=*result.pixels;
            if(!pixels.width || !pixels.height || pixels.width>64 || pixels.height>64
                || pixels.rgba.size()!=std::size_t{pixels.width}*pixels.height*4)return {};
            D3D11_TEXTURE2D_DESC description{};
            description.Width=pixels.width;description.Height=pixels.height;
            description.MipLevels=1;description.ArraySize=1;description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_IMMUTABLE;
            description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{pixels.rgba.data(),pixels.width*4,0};
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Entry entry{result.generation,{}};
            if(FAILED(device->CreateTexture2D(&description,&data,&texture))
                || FAILED(device->CreateShaderResourceView(texture.Get(),nullptr,&entry.view)))return {};
            if(cache.size()>=Store::Capacity && found==cache.end())cache.erase(cache.begin());
            found=cache.insert_or_assign(key,std::move(entry)).first;
        }
        frame.push_back(found->second.view);
        return {reinterpret_cast<ImTextureID>(found->second.view.Get()),ui::BlockIconStatus::Ready,static_cast<float>(result.pixels->width)/result.pixels->height};
    }
};
inline BlockIconGpu& blockIconGpu() {static BlockIconGpu value;return value;}
} // namespace lholo::overlay
