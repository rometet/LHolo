// GPL-3.0-or-later. Native calls are CPU fixtures, never product DLL loads.
#pragma once
#include "projection/mesh/NativeReplaySort.h"
#include <functional>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
namespace water_commands {
using namespace lholo::projection::detail;
inline std::size_t checks{},copies{},failOnCopy{},submits{},begins{};
inline bool beginFlag{};
#define WC(c) do{++checks;if(!(c)){std::cerr<<"water FAIL "<<__LINE__<<": "<<#c<<'\n';std::exit(1);}}while(0)
template<class T>struct Field{T value{};T& get(){return value;}T const& get()const{return value;}};
namespace mce {
enum class PrimitiveMode {QuadList=1,TriangleList=2};
struct MeshData {
    PrimitiveMode mMode{PrimitiveMode::QuadList};
    Field<std::vector<glm::vec3>> mPositions;
    Field<std::vector<glm::vec4>> mNormals,mTangents;
    Field<std::vector<unsigned int>> mIndices,mColors,mMERS;
    Field<std::vector<unsigned short>> mBoneId0s,mPBRTextureIndices;
    Field<std::vector<unsigned char>> mGeoType;
    std::array<Field<std::vector<glm::vec2>>,3> mTextureUVs;
    MeshData()=default;
    MeshData(MeshData const& d):mMode(d.mMode),mPositions(d.mPositions),mNormals(d.mNormals),mTangents(d.mTangents),mIndices(d.mIndices),mColors(d.mColors),mMERS(d.mMERS),mBoneId0s(d.mBoneId0s),mPBRTextureIndices(d.mPBRTextureIndices),mGeoType(d.mGeoType),mTextureUVs(d.mTextureUVs){
        if(++copies==failOnCopy)throw std::bad_alloc();
    }
    MeshData(MeshData&&)=default;MeshData& operator=(MeshData&&)=default;
};
struct MaterialPtr{int id{17};};struct TexturePtr{int id{23};};
}
struct QuadInfo {std::size_t id{};};
struct TessState {
    bool isFormatFixed{},hasNormals{},indexPhase{},noColor{},buildFaceData{},quadTwoSided{};
    unsigned char quadFacing{};int curQuadVertex{};
    unsigned int count{},maxVertexCount{};glm::vec3 faceCenterAccumulator{};
    std::vector<QuadInfo> quadInfo;
};
struct Data {
    std::unique_ptr<mce::MeshData> nativeStream;std::vector<unsigned int> derivedColors;
    TessState tessellatorState;mutable NativeReplaySortCache cameraSort;
    bool ready()const{return nativeStream && !nativeStream->mPositions.get().empty() && nativeStream->mPositions.get().size()==derivedColors.size();}
};
using PraxisCompatLiquidSectionData = Data;
struct Tessellator {
    using DebugContextCallback=std::function<void()>;
    Field<mce::MeshData> mMeshData;int mBufferResourceService{9};
    bool mIsFormatFixed{},mHasNormals{},mIndexPhase{},mNoColor{},mBuildFaceData{},mQuadTwoSided{};
    unsigned char mQuadFacing{};int mCurQuadVertex{};
    unsigned int mCount{},mMaxVertexCount{};glm::vec3 mFaceCenterAccumulator{};
    Field<std::vector<QuadInfo>> mQuadInfoList;
    explicit Tessellator(int service){WC(service==9);}
    void begin(DebugContextCallback,mce::PrimitiveMode mode,int count,bool flag){WC(mode==mce::PrimitiveMode::QuadList);WC(count>0);++begins;beginFlag=flag;}
};
struct ScreenContext{Tessellator tessellator{9};};
inline std::unique_ptr<mce::MeshData> submitted;
inline std::vector<QuadInfo> submittedMetadata;
inline std::size_t submittedCount{};
inline int emptyOffscreenCaptureDescription(){return 0;}
struct Log{template<class...T>void warn(T const&...){}};
inline Log& logger(){static Log l;return l;}
struct MeshHelpers {
    static void renderMeshImmediately(ScreenContext&,Tessellator& t,mce::MaterialPtr const& m,
        std::initializer_list<std::reference_wrapper<mce::TexturePtr const>> textures,int capture){
        WC(m.id==17);WC(textures.size()==1);WC(textures.begin()->get().id==23);WC(capture==0);WC(beginFlag);
        submitted=std::make_unique<mce::MeshData>(t.mMeshData.get());submittedMetadata=t.mQuadInfoList.get();submittedCount=t.mCount;++submits;
    }
};
#include "WaterOrderSubmit.inc"
#include "NativeCapturedPositions.inc"
inline Data makeData(std::vector<glm::vec3> p={}) {
    if(p.empty())for(float z:{1.0f,5.0f,3.0f})for(int c=0;c<4;++c)p.push_back({float(c%2),float(c/2),z});
    Data d;d.nativeStream=std::make_unique<mce::MeshData>();auto& m=*d.nativeStream;m.mPositions.get()=std::move(p);
    for(std::size_t i=0;i<m.mPositions.get().size();++i) {
        m.mColors.get().push_back(static_cast<unsigned int>(i));
        d.derivedColors.push_back(0xA0000000U+static_cast<unsigned int>(i));
        m.mTextureUVs[0].get().push_back({float(i),0});
        m.mTextureUVs[1].get().push_back({float(i),1});
        m.mTextureUVs[2].get().push_back({float(i),2});
        m.mNormals.get().push_back({float(i),1,0,0});m.mTangents.get().push_back({float(i),0,1,0});
        m.mBoneId0s.get().push_back(static_cast<unsigned short>(i));m.mPBRTextureIndices.get().push_back(static_cast<unsigned short>(i));
        m.mMERS.get().push_back(static_cast<unsigned int>(i));m.mGeoType.get().push_back(static_cast<unsigned char>(i%255));
        if(i%4==0)d.tessellatorState.quadInfo.push_back({i/4});
    }
    d.tessellatorState.maxVertexCount=static_cast<unsigned int>(m.mPositions.get().size());return d;
}
inline void verify(Data const& d,std::span<std::size_t const> order) {
    WC(submittedCount==d.nativeStream->mPositions.get().size());auto& m=*submitted;
    for(std::size_t q=0;q<order.size();++q) {
        WC(submittedMetadata[q].id==order[q]);
        for(std::size_t c=0;c<4;++c) {
            auto old=order[q]*4+c,now=q*4+c;
            WC(m.mPositions.get()[now]==d.nativeStream->mPositions.get()[old]);WC(m.mColors.get()[now]==d.derivedColors[old]);
            for(std::size_t u=0;u<3;++u)WC(m.mTextureUVs[u].get()[now]==d.nativeStream->mTextureUVs[u].get()[old]);
            WC(m.mNormals.get()[now]==d.nativeStream->mNormals.get()[old]);WC(m.mTangents.get()[now]==d.nativeStream->mTangents.get()[old]);
            WC(m.mBoneId0s.get()[now]==old);WC(m.mPBRTextureIndices.get()[now]==old);WC(m.mMERS.get()[now]==old);WC(m.mGeoType.get()[now]==old%255);
        }
    }
    for(std::size_t i=0;i<d.nativeStream->mColors.get().size();++i)WC(d.nativeStream->mColors.get()[i]==i);
}
inline void runWaterCommands() {
    ScreenContext screen;mce::MaterialPtr material;mce::TexturePtr texture;auto d=makeData();
    auto& shared=screen.tessellator.mMeshData.get();shared.mPositions.get().push_back({91,92,93});
    NativeReplaySortBudget budget;
    auto r=submitPraxisExactReplayImmediately(screen,d,material,texture,{0,0,0},budget);
    WC(r.vertices==12);WC(submits==1);WC(budget.attempts==1);WC(d.cameraSort.keyValid);
    WC(d.cameraSort.order==std::vector<std::size_t>({1,2,0}));verify(d,d.cameraSort.order);
    auto cached=d.cameraSort.order;submitPraxisExactReplayImmediately(screen,d,material,texture,{0,0,0.1f},budget);WC(budget.attempts==1);WC(d.cameraSort.order==cached);
    NativeReplaySortBudget moved;submitPraxisExactReplayImmediately(screen,d,material,texture,{0,0,10},moved);WC(moved.attempts==1);WC(d.cameraSort.order==std::vector<std::size_t>({0,2,1}));verify(d,d.cameraSort.order);
    WC(shared.mPositions.get().size()==1);WC(shared.mPositions.get()[0]==glm::vec3(91,92,93));
    auto fail=makeData();copies=0;failOnCopy=2;NativeReplaySortBudget fb;
    submitPraxisExactReplayImmediately(screen,fail,material,texture,{0,0,0},fb);failOnCopy=0;
    WC(!fail.cameraSort.keyValid);WC(fail.cameraSort.order.empty());
    std::vector<std::size_t> identity{0,1,2};verify(fail,identity);
    auto invalid=makeData();NativeReplaySortBudget ib;
    submitPraxisExactReplayImmediately(screen,invalid,material,texture,{0,0,std::numeric_limits<float>::infinity()},ib);
    WC(!invalid.cameraSort.keyValid);WC(ib.attempts==0);verify(invalid,identity);
    auto metadataBad=makeData();auto original=std::make_unique<mce::MeshData>(*metadataBad.nativeStream);auto meta=metadataBad.tessellatorState.quadInfo;meta.pop_back();
    WC(!applyNativeReplayQuadOrder(*original,meta,std::vector<std::size_t>{1,2,0}));WC(original->mPositions.get()==metadataBad.nativeStream->mPositions.get());WC(meta.size()==2);
    NativeReplaySortBudget countBudget;std::vector<NativeReplaySortCache> caches(5);
    for(auto& cache:caches){countBudget.started=std::chrono::steady_clock::now();nativeReplayQuadOrder(cache,d.nativeStream->mPositions.get(),{0,0,0},countBudget);}
    WC(countBudget.attempts==4);WC(!caches[4].keyValid);WC(caches[4].order.empty());
    auto oldOrder=d.cameraSort.order;NativeReplaySortBudget exhausted;exhausted.attempts=4;
    auto held=nativeReplayQuadOrder(d.cameraSort,d.nativeStream->mPositions.get(),{0,0,0},exhausted);WC(std::vector<std::size_t>(held.begin(),held.end())==oldOrder);
    NativeReplaySortCache timeCache;NativeReplaySortBudget timed;timed.attempts=1;timed.started=std::chrono::steady_clock::now()-std::chrono::milliseconds(2);
    WC(nativeReplayQuadOrder(timeCache,d.nativeStream->mPositions.get(),{0,0,0},timed).empty());WC(timed.attempts==1);
    auto captured=makeData(capturedPositions());NativeReplaySortBudget cb;auto cp=glm::vec3{0,80,100};
    submitPraxisExactReplayImmediately(screen,captured,material,texture,cp,cb);WC(captured.cameraSort.keyValid);verify(captured,captured.cameraSort.order);
    auto fresh=makeData();WC(!fresh.cameraSort.keyValid);WC(fresh.cameraSort.order.empty());
    std::cout<<"Water replay command fixture: PASS checks="<<checks<<" fixture_vertices="<<captured.nativeStream->mPositions.get().size()<<" native_GPU=NOT_RUN\n";
}
#undef WC
} // namespace water_commands

