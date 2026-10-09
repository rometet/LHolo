#pragma once
#include "ui/BlockIconStore.h"
#include "nlohmann/json.hpp"

namespace lholo::ui::icons {
inline bool safeTexturePath(std::string_view value) {
    return value.starts_with("textures/") && value.size()<512 && value.find("..")==value.npos
        && value.find('\\')==value.npos && value.find(':')==value.npos && value.find('\0')==value.npos;
}
class Catalog {
    nlohmann::json blocks,terrain,items;
    static nlohmann::json parse(std::string const& text) {
        if(text.size()>8*1024*1024)return {};
        return nlohmann::json::parse(text,nullptr,false,true);
    }
    static std::string texturePath(nlohmann::json const& atlas,std::string const& key,int frame) {
        if(!atlas.is_object() || !atlas.contains("texture_data"))return {};
        auto const& data=atlas["texture_data"];
        if(!data.is_object() || !data.contains(key) || !data[key].is_object() || !data[key].contains("textures"))return {};
        auto const* texture=&data[key]["textures"];
        if(texture->is_array()) {
            if(frame<0 || static_cast<std::size_t>(frame)>=texture->size())return {};
            texture=&(*texture)[static_cast<std::size_t>(frame)];
        }
        if(texture->is_object() && texture->contains("path"))texture=&(*texture)["path"];
        if(!texture->is_string())return {};
        auto path=texture->get<std::string>();return safeTexturePath(path) ? path : std::string{};
    }
public:
    Catalog(std::string const& blockText,std::string const& terrainText,std::string const& itemText)
        :blocks(parse(blockText)),terrain(parse(terrainText)),items(parse(itemText)) {}
    std::string itemPath(std::string const& nativeIconName,int nativeFrame) const {return texturePath(items,nativeIconName,nativeFrame);}
    std::string blockPath(std::string_view name) const {
        if(!blocks.is_object())return {};
        std::string key{name};
        if(!blocks.contains(key) && name.starts_with("minecraft:"))key=std::string{name.substr(10)};
        if(!blocks.contains(key) || !blocks[key].is_object() || !blocks[key].contains("textures"))return {};
        auto const* value=&blocks[key]["textures"];
        if(value->is_object()) {
            // A face texture, not a claim that all blocks have cubic geometry.
            value=nullptr;
            for(auto field:{"side","north","up","all"})if(blocks[key]["textures"].contains(field)) {value=&blocks[key]["textures"][field];break;}
        }
        if(!value || !value->is_string())return {};
        return texturePath(terrain,value->get<std::string>(),0);
    }
};
} // namespace lholo::ui::icons
