#include "structure/PlacementSession.h"
#include "structure/StructurePaths.h"
#include "io/AtomicOutput.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>
#include <limits>
#include <type_traits>

namespace lholo::structure {
bool safeSchematicPath(std::string_view value) {
    if (value.empty() || value.size() > 240 || value.front() == '/' || value.front() == '\\') return false;
    std::size_t start{};
    while (start < value.size()) {
        auto const end = value.find('/', start);
        auto const part = value.substr(start, end == value.npos ? value.size() - start : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        std::string upper;
        for (unsigned char c : part) {
            if (c < 32 || c == 127 || std::string_view{"\\:<>\"|?*"}.find(c) != std::string_view::npos) return false;
            upper += static_cast<char>(std::toupper(c));
        }
        auto const stem = upper.substr(0, upper.find('.'));
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
            || (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9')) return false;
        if (end == value.npos) break;
        start = end + 1;
        if (start == value.size()) return false;
    }
    auto ext = detail::pathFromUtf8(value).extension().wstring();
    for (auto& c : ext) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return ext == L".mcstructure" || ext == L".litematic";
}
std::filesystem::path resolveSchematicPath(std::filesystem::path const& root, std::string_view relative) {
    if (!safeSchematicPath(relative)) throw std::runtime_error("Unsafe schematic path");
    auto const canonicalRoot = std::filesystem::weakly_canonical(root);
    auto const candidate = std::filesystem::weakly_canonical(canonicalRoot / detail::pathFromUtf8(relative));
    auto const rel = candidate.lexically_relative(canonicalRoot);
    if (rel.empty() || rel.is_absolute() || *rel.begin() == L"..") throw std::runtime_error("Schematic escapes library");
    return candidate;
}
std::string placementWorldKey(std::string_view id) {
    if (id.empty() || id.size() > 96) throw std::runtime_error("Stable world ID unavailable");
    std::string result = "world-";
    for (unsigned char c : id) { result += "0123456789abcdef"[c >> 4]; result += "0123456789abcdef"[c & 15]; }
    return result;
}
std::string placementServerKey(std::string_view address, unsigned port) {
    if (address.empty() || port == 0 || port > 65535) throw std::runtime_error("Invalid server identity");
    std::string stable{address};
    for (char& c : stable) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return "server-" + placementWorldKey(stable + ":" + std::to_string(port)).substr(6);
}
namespace {
template<class T> T field(nlohmann::json const& j, char const* key, T fallback) {
    auto const it = j.find(key);
    if (it == j.end()) return fallback;
    try {
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T,bool>) {
            if (!it->is_number_integer()) return fallback;
            if (it->is_number_unsigned()) {
                auto const n=it->get<std::uint64_t>();
                if(n>static_cast<std::uint64_t>((std::numeric_limits<T>::max)()))return fallback;
            } else {
                auto const n=it->get<std::int64_t>();
                if constexpr(std::is_unsigned_v<T>){if(n<0)return fallback;}
                else if(n<(std::numeric_limits<T>::min)() || n>(std::numeric_limits<T>::max)())return fallback;
            }
        }
        return it->get<T>();
    } catch (...) { return fallback; }
}
void validate(PlacementDocument const& d) {
    if (d.placements.size() > kMaxPlacements) throw std::runtime_error("Too many placements");
    std::set<std::uint64_t> ids;
    for (auto const& p : d.placements) {
        if (!p.id || !ids.insert(p.id).second || !safeSchematicPath(p.file) || p.name.size() > 128
            || p.rotation < 0 || p.rotation > 3 || p.mirror < 0 || p.mirror > 2 || p.layer < 0
            || toInt(p.layerAxis) < 0 || toInt(p.layerAxis) > 8 || toInt(p.layerMode) < 0 || toInt(p.layerMode) > 3
            || p.origin.x < -30000000 || p.origin.x > 30000000 || p.origin.z < -30000000 || p.origin.z > 30000000
            || p.origin.y < -1000000 || p.origin.y > 1000000) throw std::runtime_error("Invalid placement");
    }
    if (d.selected && !ids.contains(d.selected)) throw std::runtime_error("Invalid selected placement");
}
}
std::string encodePlacements(PlacementDocument const& d) {
    validate(d);
    nlohmann::json j{{"version",1},{"selected",d.selected},{"placements",nlohmann::json::array()}};
    for (auto const& p : d.placements) j["placements"].push_back({{"id",p.id},{"name",p.name},{"file",p.file},
        {"dimension",p.dimension},{"origin",{p.origin.x,p.origin.y,p.origin.z}},{"rotation",p.rotation},{"mirror",p.mirror},
        {"visible",p.visible},{"countExtras",p.countExtras},{"layerAxis",toInt(p.layerAxis)},{"layerMode",toInt(p.layerMode)},{"layer",p.layer}});
    auto text = j.dump(2);
    if (text.size() > kMaxPlacementDocument) throw std::runtime_error("Placement document too large");
    return text;
}
PlacementDocument decodePlacements(std::string_view text) {
    if (text.size() > kMaxPlacementDocument) throw std::runtime_error("Placement document too large");
    auto const j = nlohmann::json::parse(text,[](int depth,auto,auto&){
        if(depth>16)throw std::runtime_error("Placement nesting too deep");return true;
    });
    if (!j.is_object() || field(j,"version",0) != 1 || !j.contains("placements") || !j["placements"].is_array())
        throw std::runtime_error("Invalid/unsupported placement document");
    if (j["placements"].size() > kMaxPlacements) throw std::runtime_error("Too many placements");
    PlacementDocument d;
    d.selected = field<std::uint64_t>(j,"selected",0);
    std::uint64_t next = 1;
    for (auto const& v : j["placements"]) {
        if (!v.is_object()) throw std::runtime_error("Invalid placement entry");
        SavedPlacement p;
        p.id = field(v,"id",next++);
        p.file = field<std::string>(v,"file",{});
        p.name = field(v,"name",p.file);
        p.dimension = field(v,"dimension",0);
        if (auto it = v.find("origin"); it != v.end()) {
            if (!it->is_array() || it->size() != 3) throw std::runtime_error("Invalid origin");
            for(auto const& n:*it)if(!n.is_number_integer() || (n.is_number_unsigned() && n.get<std::uint64_t>()>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())))throw std::runtime_error("Invalid origin coordinate");
            p.origin = {(*it)[0].get<std::int64_t>(),(*it)[1].get<std::int64_t>(),(*it)[2].get<std::int64_t>()};
        }
        p.rotation = field(v,"rotation",0) & 3;
        p.mirror = std::clamp(field(v,"mirror",0),0,2);
        p.visible = field(v,"visible",true); p.countExtras = field(v,"countExtras",true);
        p.layerAxis = layerAxisFromInt(field(v,"layerAxis",3));
        p.layerMode = layerDisplayModeFromInt(field(v,"layerMode",0));
        p.layer = (std::max)(0,field(v,"layer",0));
        d.placements.push_back(std::move(p));
    }
    if (std::none_of(d.placements.begin(),d.placements.end(),[&](auto const& p){ return p.id == d.selected; })) d.selected = 0;
    validate(d);
    return d;
}
void PlacementSession::bind(std::filesystem::path path) {
    std::lock_guard lock(mMutex);
    if (mPath == path) return;
    mPath = std::move(path); mDocument = {}; mWritable = false; ++mRevision; mStatus.clear();
    try {
        if (std::filesystem::exists(mPath)) {
            auto const bytes = std::filesystem::file_size(mPath);
            if (bytes > kMaxPlacementDocument) throw std::runtime_error("Placement document too large");
            std::ifstream in(mPath,std::ios::binary);
            if (!in) throw std::runtime_error("Cannot read placements");
            std::string text(static_cast<std::size_t>(bytes),'\0');
            in.read(text.data(),static_cast<std::streamsize>(text.size()));
            if (!in || in.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Placement document changed while reading");
            mDocument = decodePlacements(text);
        }
        mWritable = true;
    } catch (std::exception const& e) { mStatus = e.what(); }
}
PlacementSessionSnapshot PlacementSession::snapshot() const {
    std::lock_guard lock(mMutex); return {mDocument,mRevision,mWritable,mStatus};
}
void PlacementSession::bindTransient() {
    std::lock_guard lock(mMutex); mPath.clear(); mDocument = {}; mWritable = true; ++mRevision; mStatus.clear();
}
bool PlacementSession::replace(PlacementDocument document, std::uint64_t revision) {
    std::lock_guard lock(mMutex);
    if (!mWritable || revision != mRevision) return false;
    if (document == mDocument) return true;
    try {
        auto const text = encodePlacements(document);
        if (!mPath.empty()) {
            std::filesystem::create_directories(mPath.parent_path());
            io::writeOutputAtomically(mPath,[&](auto const& staged) {
            std::ofstream out(staged,std::ios::binary | std::ios::trunc);
            out.write(text.data(),static_cast<std::streamsize>(text.size())); out.flush();
            if (!out) throw std::runtime_error("Cannot write placements");
            out.close(); if (!out) throw std::runtime_error("Cannot close placements");
            return true;
            });
        }
        mDocument = std::move(document); ++mRevision; mStatus.clear(); return true;
    } catch (std::exception const& e) { mStatus = e.what(); return false; }
}
void PlacementSession::clear() {
    std::lock_guard lock(mMutex); mPath.clear(); mDocument = {}; mWritable = false; mStatus.clear(); ++mRevision;
}
} // namespace lholo::structure
