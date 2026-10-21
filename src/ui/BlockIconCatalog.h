#pragma once
#include "ui/BlockIconStore.h"
#include "nlohmann/json.hpp"

namespace lholo::ui::icons {
struct MetadataLayer {std::string blocks,terrain;};
inline bool safeTexturePath(std::string_view value) {
    return value.starts_with("textures/") && value.size()<512 && value.find("..")==value.npos
        && value.find('\\')==value.npos && value.find(':')==value.npos && value.find('\0')==value.npos;
}
class Catalog {
    nlohmann::json blocks,terrain,items;
    std::string layerOrder{"single"};
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
    static nlohmann::json merge(std::vector<nlohmann::json> const& layers,bool firstWins,bool blockIds) {
        auto result=nlohmann::json::object();
        for(std::size_t n=0;n<layers.size();++n) {
            auto const index=firstWins?layers.size()-1-n:n;
            auto const& layer=layers[index];
            for(auto it=layer.begin();it!=layer.end();++it) {
                auto key=it.key();
                if(blockIds && key.find(':')==key.npos && key!="format_version") {
                    // Same-layer explicit namespace keeps the old lookup's
                    // precedence; higher-layer legacy IDs still override lower IDs.
                    if(layer.contains("minecraft:"+key))continue;
                    key="minecraft:"+key;
                }
                // Keep unrelated IDs from lower packs. A higher definition of
                // this exact ID/atlas key replaces the whole entry, including
                // malformed definitions; never silently substitute lower art.
                result[key]=it.value();
            }
        }
        return result;
    }
    static int orderHint(std::vector<nlohmann::json> const& layers,nlohmann::json const& winner) {
        if(winner.empty())return 0;
        auto first=layers.size(),last=layers.size();
        for(std::size_t i=0;i<layers.size();++i)if(!layers[i].empty()) {
            if(first==layers.size())first=i;last=i;
        }
        if(first==layers.size())return 2;
        bool const a=layers[first]==winner,b=layers[last]==winner;
        if(!a && !b)return 2; // Not an endpoint: do not guess stack priority.
        return a==b?0:a?-1:1;
    }
public:
    Catalog(std::string const& blockText,std::string const& terrainText,std::string const& itemText)
        :blocks(parse(blockText)),terrain(parse(terrainText)),items(parse(itemText)) {}
    static std::optional<Catalog> fromLayers(std::span<MetadataLayer const> layers,
        std::string const& winningBlocks,std::string const& winningTerrain) {
        if(layers.empty() || layers.size()>512)return {};
        std::vector<nlohmann::json> blockLayers,terrainLayers,terrainFiles;
        std::size_t bytes=winningBlocks.size()+winningTerrain.size();
        for(auto const& layer:layers) {
            bytes+=layer.blocks.size()+layer.terrain.size();
            if(bytes>32*1024*1024)return {};
            auto b=layer.blocks.empty()?nlohmann::json::object():parse(layer.blocks);
            auto t=layer.terrain.empty()?nlohmann::json::object():parse(layer.terrain);
            if(!b.is_object() || !t.is_object())return {};
            if(t.contains("texture_data") && !t["texture_data"].is_object())return {};
            blockLayers.push_back(std::move(b));terrainFiles.push_back(t);
            terrainLayers.push_back(t.value("texture_data",nlohmann::json::object()));
        }
        auto const wb=winningBlocks.empty()?nlohmann::json::object():parse(winningBlocks);
        auto const wt=winningTerrain.empty()?nlohmann::json::object():parse(winningTerrain);
        if(!wb.is_object() || !wt.is_object())return {};
        if(winningBlocks.empty() && std::any_of(layers.begin(),layers.end(),[](auto const& layer){return !layer.blocks.empty();}))return {};
        if(winningTerrain.empty() && std::any_of(layers.begin(),layers.end(),[](auto const& layer){return !layer.terrain.empty();}))return {};
        auto const bh=orderHint(blockLayers,wb),th=orderHint(terrainFiles,wt);
        if(bh==2 || th==2 || (bh && th && bh!=th))return {};
        bool firstWins=bh?bh<0:th?th<0:true;
        auto b=merge(blockLayers,firstWins,true),t=merge(terrainLayers,firstWins,false);
        if(!bh && !th && (b!=merge(blockLayers,false,true) || t!=merge(terrainLayers,false,false)))return {};
        Catalog result{std::string{},std::string{},std::string{}};
        result.blocks=std::move(b);result.terrain=nlohmann::json{{"texture_data",std::move(t)}};
        result.layerOrder=bh || th?(firstWins?"first":"last"):"equivalent";
        return result;
    }
    std::string_view order() const {return layerOrder;}
    std::size_t blockCount() const {return blocks.is_object()?blocks.size():0;}
    std::size_t terrainCount() const {return terrain.is_object() && terrain.contains("texture_data")?terrain["texture_data"].size():0;}
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
