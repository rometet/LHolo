#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace lholo::ui::icons {
// Only copied CPU pixels cross the game-tick/Present boundary. No native
// objects, paths supplied by users, engine GPU handles or workers are retained.
struct Pixels { unsigned width{}, height{}; std::vector<std::uint8_t> rgba; };
struct Key { std::string block, item; auto operator<=>(Key const&) const = default; };
inline Key keyFor(std::string_view block, std::string_view item) {
    if (auto at=block.find(" ["); at!=block.npos) block=block.substr(0,at);
    return {std::string{block},std::string{item}};
}
inline bool air(Key const& key) {return key.block=="minecraft:air" || key.block=="minecraft:cave_air" || key.block=="minecraft:void_air";}
struct Request { Key key; std::uint64_t generation{}; };
struct Result { std::shared_ptr<Pixels const> pixels; bool pending{}; std::uint64_t generation{}; };
class Store {
public:
    static constexpr std::size_t Capacity=256, PendingCapacity=64;
    Result request(Key const& key) {
        std::lock_guard lock{mutex};
        if (changing || !enabled || air(key)) return {{},false,generation};
        if(auto found=entries.find(key);found!=entries.end())return {found->second,false,generation};
        if(std::find(pending.begin(),pending.end(),key)==pending.end() && !working.contains(key) && pending.size()+working.size()<PendingCapacity) pending.push_back(key);
        return {{},true,generation};
    }
    std::optional<Request> take() {
        std::lock_guard lock{mutex};
        if(changing || !enabled || pending.empty())return {};
        auto key=std::move(pending.front());pending.pop_front();working.insert(key);return Request{std::move(key),generation};
    }
    bool publish(Request const& request, std::shared_ptr<Pixels const> pixels) {
        std::lock_guard lock{mutex};
        if(changing || !enabled || request.generation!=generation)return false;
        working.erase(request.key);
        if(entries.size()>=Capacity && !entries.contains(request.key))entries.erase(entries.begin());
        entries.insert_or_assign(request.key,std::move(pixels));return true;
    }
    bool retry(Request const& request) {
        std::lock_guard lock{mutex};
        if(changing || !enabled || request.generation!=generation)return false;
        if(!working.erase(request.key))return false;
        if(std::find(pending.begin(),pending.end(),request.key)==pending.end())pending.push_front(request.key);
        return true;
    }
    void beginChange() {std::lock_guard lock{mutex};++changing;++generation;entries.clear();pending.clear();working.clear();}
    void endChange() {std::lock_guard lock{mutex};if(changing)--changing;}
    void reset(bool admit) {std::lock_guard lock{mutex};++generation;enabled=admit;entries.clear();pending.clear();working.clear();}
    std::uint64_t revision() const {std::lock_guard lock{mutex};return generation;}
    bool readable() const {std::lock_guard lock{mutex};return enabled && !changing;}
private:
    mutable std::mutex mutex;
    std::uint64_t generation{1}; unsigned changing{}; bool enabled{true};
    std::deque<Key> pending; std::set<Key> working; std::map<Key,std::shared_ptr<Pixels const>> entries;
};
inline Store& store() {static Store value;return value;}
// Convert static images to small owned RGBA thumbnails. Animated vertical
// strips without explicit frame metadata use fallback. Reject
// unsupported formats/depths and malformed or excessive buffers before copying.
inline std::shared_ptr<Pixels const> copyPixels(unsigned width,unsigned height,unsigned depth,
                                               unsigned channels,std::span<std::uint8_t const> bytes) {
    if(!width || !height || width>4096 || height>4096 || depth!=1 || (channels!=3 && channels!=4))return {};
    std::uint64_t const required=std::uint64_t{width}*height*channels;
    if(required>64*1024*1024 || bytes.size()<required)return {};
    if(height>width && height%width==0)return {};
    auto const frameHeight=height;
    auto result=std::make_shared<Pixels>();
    auto const divisor=std::max(1u,(std::max(width,frameHeight)+63)/64);
    result->width=std::max(1u,width/divisor);result->height=std::max(1u,frameHeight/divisor);
    result->rgba.resize(std::size_t{result->width}*result->height*4);
    for(unsigned y=0;y<result->height;++y)for(unsigned x=0;x<result->width;++x) {
        auto const from=(std::size_t{y*divisor}*width+x*divisor)*channels;
        auto const to=(std::size_t{y}*result->width+x)*4;
        for(unsigned c=0;c<3;++c)result->rgba[to+c]=bytes[from+c];
        result->rgba[to+3]=channels==4 ? bytes[from+3] : 255;
    }
    return result;
}
} // namespace lholo::ui::icons
