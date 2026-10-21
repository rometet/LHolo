#pragma once
#include "imgui.h"
#include <functional>
#include <string_view>

namespace lholo::ui {
enum class BlockIconStatus { Unavailable, Pending, Ready, Air };
struct BlockIconView { ImTextureID texture{}; BlockIconStatus status{BlockIconStatus::Unavailable}; float aspect{1.f}; };
// Frame-scoped image handles: the provider must own every returned texture
// until the host has submitted this frame's ImGui draw commands.
using BlockIconLookup = std::function<BlockIconView(std::string_view block, std::string_view item)>;
inline void drawBlockIcon(BlockIconLookup const& lookup, std::string_view block,
                          std::string_view item, ImVec2 position, float size) {
    if (size <= 0) return;
    auto* draw = ImGui::GetWindowDrawList();
    ImVec2 end{position.x + size, position.y + size};
    if (!ImGui::IsRectVisible(position, end)) return;
    auto const icon = lookup ? lookup(block, item) : BlockIconView{};
    draw->AddRectFilled(position, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.f);
    if (icon.status == BlockIconStatus::Ready && icon.texture) {
        auto const aspect=icon.aspect>0.f ? icon.aspect : 1.f;
        ImVec2 imageSize{aspect>1.f ? size : size*aspect,aspect>1.f ? size/aspect : size};
        ImVec2 start{position.x+(size-imageSize.x)*.5f,position.y+(size-imageSize.y)*.5f};
        draw->AddImage(icon.texture, start, {start.x+imageSize.x,start.y+imageSize.y});
    } else {
        char const* label = icon.status == BlockIconStatus::Air ? "-" : "?";
        auto const text = ImGui::CalcTextSize(label);
        draw->AddText({position.x + (size-text.x)*.5f, position.y + (size-text.y)*.5f},
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
    }
}
} // namespace lholo::ui
