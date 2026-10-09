#include "ui/BlockIconRuntime.h"
#include "ui/BlockIconCatalog.h"
#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "app/ScopeExit.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourcePackStack.h"
#include "mc/resources/PackInstance.h"
#include "mc/resources/ResourcePack.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/deps/core/image/Image.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/file/Path.h"
#include "mc/deps/core/resource/ResourceFileSystem.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include <atomic>
#include <shared_mutex>

namespace lholo::ui::icons {
namespace {
bool composeInstalled{}, reloadInstalled{}, imagesInstalled{};
std::atomic_flag ticking=ATOMIC_FLAG_INIT;
// No native object is stored here: address is only a context identity token.
std::uintptr_t lastClient{};
std::uint64_t catalogGeneration{};
std::unique_ptr<Catalog> catalog;
unsigned decodeDiagnostics{};

LL_TYPE_INSTANCE_HOOK(IconPackComposeHook,ll::memory::HookPriority::Normal,
    ResourcePackManager,&ResourcePackManager::_composeFullStack,void) {
    app::hook_lifecycle::DetourGuard guard;
    if(!guard){origin();return;}
    store().beginChange();app::ScopeExit end{[]() noexcept {store().endChange();}};
    origin();
}
LL_TYPE_INSTANCE_HOOK(IconTextureReloadHook,ll::memory::HookPriority::Normal,
    mce::TextureGroup,&mce::TextureGroup::reloadAllTextures,void) {
    app::hook_lifecycle::DetourGuard guard;
    if(!guard){origin();return;}
    store().beginChange();app::ScopeExit end{[]() noexcept {store().endChange();}};
    origin();
}
LL_TYPE_INSTANCE_HOOK(IconImagesReloadHook,ll::memory::HookPriority::Normal,
    mce::TextureGroup,&mce::TextureGroup::reloadImages,void,
    gsl::span<ResourceLocationPair> asyncImages,gsl::span<ResourceLocation> immediateImages,
    std::vector<ResourceLocation> keep,ImageCacheMode mode) {
    app::hook_lifecycle::DetourGuard guard;
    if(!guard){origin(asyncImages,immediateImages,std::move(keep),mode);return;}
    store().beginChange();app::ScopeExit end{[]() noexcept {store().endChange();}};
    origin(asyncImages,immediateImages,std::move(keep),mode);
}
std::string text(ResourcePackManager const& manager,char const* path) {
    std::string value;
    if(!manager.loadText(ResourceLocation{Core::PathView{path},ResourceFileSystem::UserPackage},value))return {};
    return value;
}
struct CatalogRead {std::unique_ptr<Catalog> value;bool busy{};std::size_t layers{};};
CatalogRead readCatalog(ResourcePackManager& manager,std::uint64_t generation) {
    struct PinnedPack {std::shared_ptr<ResourcePack> pack;int subpack{};};
    std::vector<PinnedPack> packs;
    {
        // Short ownership snapshot only. Never call a native resource loader
        // while holding the engine's stack mutex or retain PackInstance refs.
        std::shared_lock lock{manager.mFullStackAccess.get(),std::try_to_lock};
        if(!lock.owns_lock())return {{},true};
        if(!store().readable() || store().revision()!=generation)return {{},true};
        auto const& stack=manager.mFullStack.get();
        if(!stack)return {{},true};
        auto const& instances=stack->mStack.get();
        if(instances.size()>512)return {};
        packs.reserve(instances.size());
        for(auto const& instance:instances) {
            std::shared_ptr<ResourcePack> owned=instance.mPack.get();
            packs.push_back({std::move(owned),instance.mSubpackIndex});
        }
    }
    std::vector<MetadataLayer> layers;layers.reserve(packs.size());
    std::size_t bytes{};
    for(auto const& pinned:packs) {
        if(!store().readable() || store().revision()!=generation)return {{},true};
        MetadataLayer layer;
        if(!pinned.pack->getResource(Core::Path{"blocks.json"},layer.blocks,pinned.subpack))layer.blocks.clear();
        if(!pinned.pack->getResource(Core::Path{"textures/terrain_texture.json"},layer.terrain,pinned.subpack))layer.terrain.clear();
        bytes+=layer.blocks.size()+layer.terrain.size();
        if(layer.blocks.size()>8*1024*1024 || layer.terrain.size()>8*1024*1024 || bytes>32*1024*1024)return {};
        layers.push_back(std::move(layer));
    }
    auto const blockWinner=text(manager,"blocks.json"),terrainWinner=text(manager,"textures/terrain_texture.json");
    if(!store().readable() || store().revision()!=generation)return {{},true};
    auto merged=Catalog::fromLayers(layers,blockWinner,terrainWinner);
    return {merged?std::make_unique<Catalog>(std::move(*merged)):nullptr,false,layers.size()};
}
}
bool installHooks() {
    store().reset(false);
    composeInstalled=IconPackComposeHook::hook()==0;
    if(composeInstalled)reloadInstalled=IconTextureReloadHook::hook()==0;
    if(reloadInstalled)imagesInstalled=IconImagesReloadHook::hook()==0;
    if(!imagesInstalled)return false; // Kernel's tracked rollback removes partial installs.
    store().reset(true);return true;
}
bool uninstallHooks() {
    store().reset(false);
    bool ok=true;
    if(imagesInstalled) {if(IconImagesReloadHook::unhook())imagesInstalled=false;else ok=false;}
    if(reloadInstalled) {if(IconTextureReloadHook::unhook())reloadInstalled=false;else ok=false;}
    if(composeInstalled) {if(IconPackComposeHook::unhook())composeInstalled=false;else ok=false;}
    return ok;
}
void closeSession() {store().reset(false);catalog.reset();catalogGeneration=0;lastClient=0;decodeDiagnostics=0;}
void tick() {
    if(!store().readable() || ticking.test_and_set(std::memory_order_acquire))return;
    app::ScopeExit untick{[]() noexcept {ticking.clear(std::memory_order_release);}};
    auto client=ll::service::getClientInstance();
    if(!client)return;
    auto const identity=reinterpret_cast<std::uintptr_t>(&*client);
    if(lastClient && identity!=lastClient)store().reset(true);
    lastClient=identity;
    // Native loaders return owned strings/images. Pack pins protect ownership;
    // the SDK does not document in-place cache mutation synchronization, so
    // concurrent pack switching still needs real-client validation. Do not read BlockGraphics/ItemRenderer registries,
    // cached ImageBuffer pointers or game GPU textures during a reload.
    auto& manager=client->getResourcePackManager();
    for(unsigned budget=0;budget<2;++budget) {
        auto const request=store().take();if(!request)return;
        bool finished=false;
        app::ScopeExit finishRequest{[&]() noexcept {
            if(!finished)try {(void)store().publish(*request,{});}catch(...) {}
        }};
        if(catalogGeneration!=request->generation) {
            auto next=readCatalog(manager,request->generation);
            if(next.busy || store().revision()!=request->generation || !store().readable()) {
                (void)store().retry(*request);finished=true;return;
            }
            catalog=std::move(next.value);catalogGeneration=request->generation;decodeDiagnostics=0;
            LHolo::getInstance().getSelf().getLogger().info(
                "BLOCK_ICON_CATALOG generation={} layers={} order={} blocks={} terrain={} ready={}",
                catalogGeneration,next.layers,catalog?catalog->order():std::string_view{"unresolved"},
                catalog?catalog->blockCount():0,catalog?catalog->terrainCount():0,static_cast<bool>(catalog));
        }
        // Exact active-stack definitions, merged per ID using priority checked
        // against the engine loader's winning files. No guessed vanilla paths.
        auto const path=catalog?catalog->blockPath(request->key.block):std::string{};
        std::shared_ptr<Pixels const> pixels;
        if(!path.empty()) {
            auto image=manager.loadTexture(ResourceLocation{Core::PathView{path},ResourceFileSystem::UserPackage});
            unsigned const channels=image.imageFormat==mce::ImageFormat::RGBA8Unorm ? 4
                :image.imageFormat==mce::ImageFormat::RGB8Unorm ? 3 : 0;
            auto bytes=image.mImageBytes.getSpan();
            pixels=copyPixels(image.mWidth,image.mHeight,image.mDepth,channels,
                {bytes.data(),bytes.size()});
            if(decodeDiagnostics++<8)LHolo::getInstance().getSelf().getLogger().info(
                "BLOCK_ICON_DECODE block={} path={} format={} width={} height={} depth={} bytes={} ready={}",
                request->key.block,path,static_cast<unsigned>(image.imageFormat),image.mWidth,image.mHeight,image.mDepth,
                bytes.size(),static_cast<bool>(pixels));
        }
        (void)store().publish(*request,std::move(pixels));finished=true;
    }
}
} // namespace lholo::ui::icons
