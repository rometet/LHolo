#include "ui/BlockIconRuntime.h"
#include "ui/BlockIconCatalog.h"
#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "app/ScopeExit.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/deps/core/image/Image.h"
#include "mc/deps/core/file/PathView.h"
#include <atomic>

namespace lholo::ui::icons {
namespace {
bool composeInstalled{}, reloadInstalled{}, imagesInstalled{};
std::atomic_flag ticking=ATOMIC_FLAG_INIT;
// No native object is stored here: address is only a context identity token.
std::uintptr_t lastClient{};
std::uint64_t catalogGeneration{};
std::unique_ptr<Catalog> catalog;

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
    if(!manager.loadText(ResourceLocation{Core::PathView{path}},value))return {};
    return value;
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
void closeSession() {store().reset(false);catalog.reset();catalogGeneration=0;lastClient=0;}
void tick() {
    if(!store().readable() || ticking.test_and_set(std::memory_order_acquire))return;
    app::ScopeExit untick{[]() noexcept {ticking.clear(std::memory_order_release);}};
    auto client=ll::service::getClientInstance();
    if(!client)return;
    auto const identity=reinterpret_cast<std::uintptr_t>(&*client);
    if(lastClient && identity!=lastClient)store().reset(true);
    lastClient=identity;
    // Native resource loader returns owned strings/images. Pack-stack
    // access relies on the engine loader API; concurrent pack switching still
    // needs real-client validation. Do not read BlockGraphics/ItemRenderer registries,
    // cached ImageBuffer pointers or game GPU textures during a reload.
    auto& manager=client->getResourcePackManager();
    for(unsigned budget=0;budget<2;++budget) {
        auto const request=store().take();if(!request)return;
        bool finished=false;
        app::ScopeExit finishRequest{[&]() noexcept {
            if(!finished)try {(void)store().publish(*request,{});}catch(...) {}
        }};
        if(catalogGeneration!=request->generation) {
            auto blockText=text(manager,"blocks.json");
            auto terrainText=text(manager,"textures/terrain_texture.json");
            auto next=std::make_unique<Catalog>(blockText,terrainText,std::string{});
            if(store().revision()!=request->generation || !store().readable())return;
            catalog=std::move(next);catalogGeneration=request->generation;
        }
        // Exact active-pack metadata only. Missing/partial overrides stay '?'
        // rather than guessing a vanilla path or reading unknown SDK fields.
        auto const path=catalog->blockPath(request->key.block);
        std::shared_ptr<Pixels const> pixels;
        if(!path.empty()) {
            auto image=manager.loadTexture(ResourceLocation{Core::PathView{path}});
            unsigned const channels=image.imageFormat==mce::ImageFormat::RGBA8Unorm ? 4
                :image.imageFormat==mce::ImageFormat::RGB8Unorm ? 3 : 0;
            auto bytes=image.mImageBytes.getSpan();
            pixels=copyPixels(image.mWidth,image.mHeight,image.mDepth,channels,
                {bytes.data(),bytes.size()});
        }
        (void)store().publish(*request,std::move(pixels));finished=true;
    }
}
} // namespace lholo::ui::icons
