#pragma once
// Do not short-circuit ScreenView::render: it owns native UI bindings and the
// controller updates required for real inventory transfer acknowledgement.
// Pass this display-sink context instead, so those updates still execute while
// the material-grabber container draws no visible UI.
#include "mc/client/gui/controls/UIRenderContext.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/client/gui/UITextureInfoPtr.h"
#include "mc/deps/input/RectangleArea.h"
#include <optional>
#include <string>
#include <utility>

namespace lholo::structure::detail {

class InvisibleContainerRenderContext final : public ::UIRenderContext {
    ::UIRenderContext& delegate;
    float textAlpha;
public:
    explicit InvisibleContainerRenderContext(::UIRenderContext& context)
        : delegate(context), textAlpha(context.getTextAlpha()) {}
    int getLineLength(::Font& font, ::std::string const& text,
        float fontSize, bool showColorSymbol) const override {
        return delegate.getLineLength(font,text,fontSize,showColorSymbol);
    }
    float getTextAlpha() const override { return textAlpha; }
    void setTextAlpha(float alpha) override { textAlpha=alpha; }
    void drawDebugText(::RectangleArea const&,::std::string&&,
        ::mce::Color const&,float,::ui::TextAlignment,
        ::TextMeasureData const&,::CaretMeasureData const&) override {}
    void drawText(::Font&,::RectangleArea const&,::std::string&&,
        ::mce::Color const&,float,::ui::TextAlignment,
        ::TextMeasureData const&,::CaretMeasureData const&) override {}
    void flushText(float,::std::optional<float>) override {}
    void drawImage(::mce::ClientTexture const&,
        ::glm::vec2 const&,::glm::vec2 const&,::glm::vec2 const&,
        ::glm::vec2 const&,bool) override {}
    void drawNineslice(::mce::ClientTexture const&,::NinesliceInfo const&) override {}
    void flushImages(::mce::Color const&,float,::HashedString const&) override {}
    void beginSharedMeshBatch(::ComponentRenderBatch&) override {}
    void endSharedMeshBatch(::ComponentRenderBatch&) override {}
    void reserveSharedMeshBatch(uint64) override {}
    uint64 getSharedMeshBatchVertexCount() const override { return 0; }
    void drawRectangle(::RectangleArea const&,::mce::Color const&,float,int) override {}
    void fillRectangle(::RectangleArea const&,::mce::Color const&,float) override {}
    void increaseStencilRef() override {}
    void decreaseStencilRef() override {}
    void resetStencilRef() override {}
    void fillRectangleStencil(::RectangleArea const&) override {}
    void enableScissorTest(::RectangleArea const&) override {}
    void disableScissorTest() override {}
    void setClippingRectangle(::RectangleArea const&) override {}
    void setFullClippingRectangle() override {}
    void saveCurrentClippingRectangle() override {}
    void restoreSavedClippingRectangle() override {}
    ::RectangleArea getFullClippingRectangle() const override {
        return delegate.getFullClippingRectangle();
    }
    bool updateCustom(::gsl::not_null<::CustomRenderComponent*> renderer) override {
        return delegate.updateCustom(renderer);
    }
    void renderCustom(::gsl::not_null<::CustomRenderComponent*>,int) override {}
    void cleanup() override {}
    void removePersistentMeshes() override {}
    ::mce::TexturePtr getTexture(::ResourceLocation const& resourceLocation,
        bool forceReload) const override {
        return delegate.getTexture(resourceLocation,forceReload);
    }
    ::mce::TexturePtr getZippedTexture(::Core::Path const& folder,
        ::ResourceLocation const& resourceLocation,bool forceReload) const override {
        return delegate.getZippedTexture(folder,resourceLocation,forceReload);
    }
    bool unloadTexture(::ResourceLocation const& resourceLocation) override {
        return delegate.unloadTexture(resourceLocation);
    }
    ::UITextureInfoPtr getUITextureInfo(::ResourceLocation const& resourceLocation,
        bool forceReload) const override {
        return delegate.getUITextureInfo(resourceLocation,forceReload);
    }
    void touchTexture(::ResourceLocation const& resourceLocation) override {
        delegate.touchTexture(resourceLocation);
    }
    ::UIMeasureStrategy& getMeasureStrategy() override {
        return delegate.getMeasureStrategy();
    }
    void snapImageSizeToGrid(::glm::vec2& size) const override {
        delegate.snapImageSizeToGrid(size);
    }
    void snapImagePositionToGrid(::glm::vec2& position) const override {
        delegate.snapImagePositionToGrid(position);
    }
    void notifyImageEstimate(uint64 imageCount) override {
        // No image draw calls are emitted by the display sink.
        (void)imageCount;
    }
};

} // namespace lholo::structure::detail
