// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Shared lookups for overlay geometry whose color lives in vertex data: the
// glow sign text material (reads COLOR0, emissive constant color, native depth
// bias) and the vanilla 2x2 pure-white texture (neutral texture multiply plus
// a permanently-passing alpha test). Used by the projection renderer and the
// bounds wireframe.

#pragma once

#include <optional>
#include <string_view>
#include <variant>

#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/renderer/RenderMaterialInfo.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/RenderMaterial.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"

namespace lholo::render {

using TextureVariant = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;

inline bool materialExists(mce::MaterialPtr const& material) {
    return material.mRenderMaterialInfoPtr.get() != nullptr;
}

// The vanilla 2x2 pure-white texture. Sampled at its center it reads exactly
// (1,1,1,1): the texture multiply stays neutral and alpha tests never discard.
// TextureGroup owns its cache. Resolve on each submission so no LHolo static
// handle survives a resource reload, renderer replacement or DLL teardown.
inline TextureVariant resolveWhiteTextureVariant(LevelRenderer* levelRenderer) {
    if (levelRenderer) {
        auto const& textureGroup = levelRenderer->mTextureGroup.get();
        if (textureGroup) {
            auto texture = textureGroup->getTexture(
                ResourceLocation{Core::PathView{"textures/ui/white_background"}},
                false,
                std::nullopt,
                cg::TextureSetLayerType::Color
            );
            auto const& clientTexture = texture.mClientTexture;
            if (clientTexture && clientTexture->mIsMissingTexture != IsMissingTexture::Yes) {
                return TextureVariant{std::move(texture)};
            }
        }
    }
    return TextureVariant{};
}

// MaterialPtr has no usable default constructor in the client ABI. Its copy
// constructor does construct the owning shared_ptr, unlike a cast from zeroed
// bytes. Copy a live engine handle, then replace only the material reference.
// Return a submission-scoped owner rather than a static cache: reloads can
// replace the engine's table and shutdown must not destroy stale GPU handles.
inline std::optional<mce::MaterialPtr> resolveNamedMaterial(
    std::string_view name,
    mce::MaterialPtr const& exemplar
) {
    for (auto* group : {&mce::RenderMaterialGroup::common(),
                        &mce::RenderMaterialGroup::switchable()}) {
        for (auto const& [key, info] : group->mMaterials.get()) {
            if (!info || !info->mPtr || key.getString() != name) continue;
            mce::MaterialPtr result{exemplar};
            result.mRenderMaterialInfoPtr = info;
            return result;
        }
    }
    return std::nullopt;
}

inline std::optional<mce::MaterialPtr> resolveGlowSignMaterial(mce::MaterialPtr const& exemplar) {
    return resolveNamedMaterial("glow_sign_text", exemplar);
}

// Exact Replay keeps sign_text separate from glow_sign_text: their shaders
// have different contracts. A missing exact name keeps the existing fallback.
inline std::optional<mce::MaterialPtr> resolveSignTextMaterial(mce::MaterialPtr const& exemplar) {
    return resolveNamedMaterial("sign_text", exemplar);
}

inline std::optional<mce::MaterialPtr> resolveTerrainBlendMaterial(mce::MaterialPtr const& exemplar) {
    return resolveNamedMaterial("terrain_blend", exemplar);
}

} // namespace lholo::render
