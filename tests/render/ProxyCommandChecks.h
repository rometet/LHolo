// CPU command tags for the verbatim procedural fallback owner. These are not
// SDK/material/GPU ownership doubles and do not establish native appearance.
#include <map>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

struct IntTag { int data{}; };
struct CompoundTag;
struct Tag {
    enum class Type { Int, Compound };
    std::variant<IntTag, std::shared_ptr<CompoundTag>> value;
    template<class T> bool hold() const { return std::holds_alternative<std::shared_ptr<T>>(value); }
    template<class T> T const& get() const {
        if constexpr(std::is_same_v<T, CompoundTag>) return *std::get<std::shared_ptr<CompoundTag>>(value);
        else return std::get<T>(value);
    }
    Type getId() const { return std::holds_alternative<IntTag>(value) ? Type::Int : Type::Compound; }
};
struct CompoundTag : std::map<std::string, Tag> {};
template<class T> struct CommandValue {
    T value{};
    T& get() { return value; }
    T const& get() const { return value; }
};
struct Vec3 { float x{}, y{}, z{}; bool operator==(Vec3 const&) const = default; };
struct BlockPos { int x{}, y{}, z{}; };
struct BoundingBox {};
struct LegacyStructureSettings { int mirror{}, rotation{}; void* owner{}; BoundingBox bounds; };
struct UvTag { float _u0{}, _v0{}, _u1{}, _v1{}; };
struct Block;
struct BlockGraphics {
    UvTag still{.10F,.20F,.30F,.40F}, flow{.50F,.60F,.70F,.80F};
    UvTag const& getTexture(int face, int) const { return face==2 ? flow : still; }
    static BlockGraphics const* getForBlock(Block const&);
};
struct Block {
    std::string name;
    struct Type { struct { bool mSuperHot{}; } mMaterial; bool mIsOpaqueFullBlock{}; } type;
    CommandValue<CompoundTag> mSerializationId;
    BlockGraphics graphics;
    bool air{};
    explicit Block(std::string n, int depth=0, bool opaque=false) : name(std::move(n)) {
        type.mMaterial.mSuperHot = name.find("lava") != std::string::npos;
        type.mIsOpaqueFullBlock=opaque;
        auto states=std::make_shared<CompoundTag>();
        states->emplace("liquid_depth",Tag{IntTag{depth}});
        mSerializationId.value.emplace("states",Tag{states});
    }
    std::string const& getTypeName() const { return name; }
    Type const& getBlockType() const { return type; }
    bool isAir() const { return air; }
};
inline BlockGraphics const* BlockGraphics::getForBlock(Block const& b) { return &b.graphics; }
namespace lholo::structure {
struct LoadedStructure {
    struct RenderBlock { int x{},y{},z{}; Block const* block{}; Block const* liquid{}; };
    std::vector<RenderBlock> renderBlocks;
};
}
struct VertexCommand { Vec3 position; std::array<float,2> uv; std::uint32_t color{}; };
namespace mce {
enum class PrimitiveMode { QuadList };
struct Mesh { std::vector<VertexCommand> vertices; explicit Mesh(std::vector<VertexCommand> v): vertices(std::move(v)) {} };
}
struct SupplementaryFieldAutoGenerationMode { int mode{}; };
struct Tessellator {
    enum class UploadMode { Never };
    struct DebugContextCallback {};
    std::vector<VertexCommand> vertices;
    std::array<float,2> uv{};
    std::uint32_t packed{};
    void begin(DebugContextCallback,mce::PrimitiveMode,int,bool) { vertices.clear(); }
    void color(float r,float g,float b,float a) {
        auto byte=[](float f){return static_cast<std::uint32_t>(std::lround(f*255.F));};
        packed=byte(r)|(byte(g)<<8U)|(byte(b)<<16U)|(byte(a)<<24U);
    }
    void tex2(std::array<float,2> v) { uv=v; }
    void vertex(float x,float y,float z) { vertices.push_back({{x,y,z},uv,packed}); }
    std::vector<VertexCommand> end(UploadMode,char const*,SupplementaryFieldAutoGenerationMode) { return vertices; }
};
enum class CorrectionState { Missing, Correct };
struct ProjectionSectionBuildSettings {
    int mirrorMode{},rotationTurns{},offsetX{},offsetY{},offsetZ{},mirror{},rotation{};
    float structureOpacity{1.F}; bool identityTransform{true};
};
struct ProjectionState {
    std::shared_ptr<lholo::structure::LoadedStructure> structure;
    BlockPos anchor;
    std::vector<std::vector<std::size_t>> sectionBlockIndices;
    std::vector<CorrectionState> corrections;
    std::shared_ptr<std::map<std::tuple<int,int,int>,std::size_t>> expectedWorldBlockIndices;
    std::vector<std::size_t> liquidProxySectionCellCounts;
    std::vector<std::unique_ptr<mce::Mesh>> liquidProxySectionMeshes;
    struct { std::size_t liquidProxyFallbackCells{}; } nativeLiquidTelemetry;
    CorrectionState buildCorrectionState(std::size_t i) const { return corrections.at(i); }
};
inline Block const* transformExpectedBlock(Block const* b,LegacyStructureSettings const&,bool) { return b; }
inline BlockPos transformStructurePosition(lholo::structure::LoadedStructure::RenderBlock const& b,
    lholo::structure::LoadedStructure const&,int,int) { return {b.x,b.y,b.z}; }
inline constexpr std::uint32_t LiquidWaterTintAbgrRgb=0x00e4763fU, LiquidLavaTintAbgrRgb=0x001080ffU;
#include "ProxyColorEmitter.inc"
namespace proxy_current {
namespace structure=lholo::structure;
#include "CurrentProxyBody.inc"
}
namespace proxy_opacity_stage {
namespace structure=lholo::structure;
#include "OpacityStageProxyBody.inc"
}
inline ProjectionState proxyState(std::vector<lholo::structure::LoadedStructure::RenderBlock> blocks,
                                 std::vector<std::size_t> section) {
    ProjectionState s;
    s.structure=std::make_shared<lholo::structure::LoadedStructure>();
    s.structure->renderBlocks=std::move(blocks);
    s.sectionBlockIndices={std::move(section)};
    s.corrections.assign(s.structure->renderBlocks.size(),CorrectionState::Missing);
    s.expectedWorldBlockIndices=std::make_shared<std::map<std::tuple<int,int,int>,std::size_t>>();
    for(std::size_t i=0;i<s.structure->renderBlocks.size();++i) {
        auto const& b=s.structure->renderBlocks[i];s.expectedWorldBlockIndices->emplace(std::tuple{b.x,b.y,b.z},i);
    }
    s.liquidProxySectionCellCounts.resize(1);s.liquidProxySectionMeshes.resize(1);return s;
}
inline void proxyCommandChecks() {
    Block water{"minecraft:water"}, lava{"minecraft:lava"};
    for(auto liquid:{&water,&lava}) for(float opacity:{1.F,.5F,.01F,0.F}) {
        auto a=proxyState({{15,0,0,nullptr,liquid}},{0});
        auto b=proxyState({{15,0,0,nullptr,liquid}},{0});
        Tessellator ta,tb;ProjectionSectionBuildSettings settings;settings.structureOpacity=opacity;
        proxy_opacity_stage::buildLiquidProxySectionMesh(a,ta,0,Tessellator::UploadMode::Never,settings,{});
        proxy_current::buildLiquidProxySectionMesh(b,tb,0,Tessellator::UploadMode::Never,settings,{});
        CHECK(a.liquidProxySectionMeshes[0]->vertices.size()==24 && b.liquidProxySectionMeshes[0]->vertices.size()==24);
        for(std::size_t i=0;i<24;++i) {
            auto const& av=a.liquidProxySectionMeshes[0]->vertices[i];auto const& bv=b.liquidProxySectionMeshes[0]->vertices[i];
            CHECK(av.position==bv.position && av.uv==bv.uv);
            CHECK((av.color & 0xff000000U)==(bv.color & 0xff000000U));
            if(i>=4 && i<8) CHECK(av.color==bv.color); // accepted top tint
        }
        auto const& v=b.liquidProxySectionMeshes[0]->vertices;
        CHECK(v[0].color==liquidProxyFaceColor(v[4].color,LiquidProxyFace::Bottom));
        CHECK(v[8].color==liquidProxyFaceColor(v[4].color,LiquidProxyFace::NorthSouth));
        CHECK(v[16].color==liquidProxyFaceColor(v[4].color,LiquidProxyFace::EastWest));
    }
    // Baseline treated every non-air body as full/opaque. Reproduce the
    // missing bottom over partial/transparent bodies, retaining real occluders.
    for(bool opaque:{false,true}) {
        Block below{"fixture:body",0,opaque};
        auto a=proxyState({{0,0,0,nullptr,&water},{0,-1,0,&below,nullptr}},{0});
        auto b=proxyState({{0,0,0,nullptr,&water},{0,-1,0,&below,nullptr}},{0});
        Tessellator ta,tb;ProjectionSectionBuildSettings settings;
        proxy_opacity_stage::buildLiquidProxySectionMesh(a,ta,0,Tessellator::UploadMode::Never,settings,{});
        proxy_current::buildLiquidProxySectionMesh(b,tb,0,Tessellator::UploadMode::Never,settings,{});
        CHECK(a.liquidProxySectionMeshes[0]->vertices.size()==20);
        CHECK(b.liquidProxySectionMeshes[0]->vertices.size()==(opaque?20U:24U));
    }
    // Independent sections share the immutable world lookup at x=15/16.
    // Same-liquid boundaries are suppressed, hidden-layer lookup entries are
    // absent, and cells owned by native replay never gain a duplicate proxy.
    for(int x:{-17,-1,15}) {
        for(std::size_t sectionCell:{0U,1U}) {
            auto s=proxyState({{x,0,0,nullptr,&water},{x+1,0,0,nullptr,&water}},{sectionCell});
            Tessellator t;ProjectionSectionBuildSettings settings;
            proxy_current::buildLiquidProxySectionMesh(s,t,0,Tessellator::UploadMode::Never,settings,{});
            CHECK(s.liquidProxySectionMeshes[0]->vertices.size()==20);
        }
        auto s=proxyState({{x,0,0,nullptr,&water},{x+1,0,0,nullptr,&water}},{0});
        s.expectedWorldBlockIndices->erase(std::tuple{x+1,0,0});
        Tessellator t;ProjectionSectionBuildSettings settings;
        proxy_current::buildLiquidProxySectionMesh(s,t,0,Tessellator::UploadMode::Never,settings,{});
        CHECK(s.liquidProxySectionMeshes[0]->vertices.size()==24);
        std::array<std::size_t,1> nativeOwner{0};
        proxy_current::buildLiquidProxySectionMesh(s,t,0,Tessellator::UploadMode::Never,settings,nativeOwner);
        CHECK(!s.liquidProxySectionMeshes[0] && s.liquidProxySectionCellCounts[0]==0);
    }
}
