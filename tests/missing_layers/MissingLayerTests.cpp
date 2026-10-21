#include "projection/core/ProjectionMissingLayers.h"
#include "projection/mesh/SectionMissingLayerSnapshot.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
using namespace lholo::projection::detail;
enum class CorrectionState { Unknown, Missing, Correct, WrongType, WrongState };
struct BlockPos { int x{}, y{}, z{}; };
struct Block {
    std::string name; int state{};
    bool isAir() const { return name == "minecraft:air"; }
    std::string const& getTypeName() const { return name; }
};
namespace block { std::string placeableBaseName(std::string const& n) { return n; } }
namespace structure { bool placeholderBlock(std::string const& n) { return n == "placeholder"; } }
struct Region {
    Block body{"minecraft:air"}, liquid{"minecraft:air"}; bool loaded{true};
    Block const& getBlock(BlockPos const&) const { return body; }
    Block const& getLiquidBlock(BlockPos const&) const { return liquid; }
    Block const& getExtraBlock(BlockPos const&) const { return liquid; }
    bool areChunksFullyLoaded(BlockPos const&, int) const { return loaded; }
};
Block const& withFlattenedConnections(Block const& b, Region&, BlockPos const&) { return b; }
bool projectionStatesMatch(Block const& a, Block const& b) { return a.state == b.state; }
struct Result { CorrectionState correction; std::uint8_t missing; };
Result classify(Block const* expected, Block const* expectedLiquid, Region& region) {
    BlockPos position{};
#include "Classify.inc"
    return {nextState, nextMissingLayers};
}
struct Section { bool dirty{}, incrementalDirty{}; std::uint64_t requestedRevision{}; };
struct ProjectionState {
    std::vector<CorrectionState> correctionStates;
    std::vector<std::uint8_t> missingLayers;
    std::vector<std::size_t> blockToSection;
    std::vector<Section> sections;
    std::shared_ptr<std::map<std::tuple<int,int,int>,std::size_t>> expectedWorldBlockIndices;
};
#include "MarkDirty.inc"
bool sectionStorageContains(ProjectionState const& state, std::size_t i) { return i < state.sections.size(); }
void commit(ProjectionState& state, Result value, bool visible = true) {
    auto const index = std::size_t{0}; auto const nextState=value.correction;
    auto const nextMissingLayers=value.missing; BlockPos position{15,0,0};
#include "CacheCommit.inc"
}
struct WorkerResult { std::uint64_t revision{}; };
bool stale(WorkerResult result, ProjectionState const& state, std::size_t section) {
#include "Revision.inc"
}
static unsigned checks{};
void require(bool ok) { ++checks; if (!ok) { std::cerr << "FAIL check=" << checks << '\n'; std::exit(1); } }
int main() {
    Block const water{"minecraft:water"};
    for (auto family : {"slab", "stairs", "trapdoor", "fence", "pane"}) {
        Block const body{family};
        for (int presence=0; presence<4; ++presence) {
            Region region;
            if (presence&1) region.body=body;
            if (presence&2) region.liquid=water;
            auto const value=classify(&body,&water,region);
            require(value.correction==(presence==3 ? CorrectionState::Correct : CorrectionState::Missing));
            require(value.missing==projectionMissingLayerMask(!(presence&1),!(presence&2)));
            require(projectionBodyShouldRender(value.correction,value.missing)==!(presence&1));
            require(projectionLiquidShouldRender(value.correction,value.missing)==!(presence&2));
        }
        Region wrong{body,water};wrong.body.state=1;
        require(classify(&body,&water,wrong).correction==CorrectionState::WrongState);
        wrong.body.name="other";
        require(classify(&body,&water,wrong).correction==CorrectionState::WrongType);
        wrong.liquid={"minecraft:air"};
        auto const missing=classify(&body,&water,wrong);
        require(missing.correction==CorrectionState::Missing);
        require(!projectionBodyShouldRender(missing.correction,missing.missing));
        require(projectionLiquidShouldRender(missing.correction,missing.missing));
        wrong.loaded=false;
        auto const unknown=classify(&body,&water,wrong);
        require(unknown.correction==CorrectionState::Unknown);
        require(projectionBodyShouldRender(unknown.correction,unknown.missing));
        require(!projectionLiquidShouldRender(unknown.correction,unknown.missing));
        Region dry{body,{"minecraft:air"}};
        require(classify(&body,nullptr,dry).correction==CorrectionState::Correct);
    }
    ProjectionState state{{CorrectionState::Missing,CorrectionState::Missing},{MissingLayerBoth,MissingLayerBoth},
        {0,1},std::vector<Section>(2),std::make_shared<std::map<std::tuple<int,int,int>,std::size_t>>()};
    (*state.expectedWorldBlockIndices)[{16,0,0}]=1;
    SectionMissingLayerSnapshot snapshot;snapshot.capture({0},state.missingLayers);
    WorkerResult inFlight{state.sections[0].requestedRevision};
    commit(state,{CorrectionState::Missing,MissingLayerLiquid});
    require(state.correctionStates[0]==CorrectionState::Missing);
    require(state.sections[0].requestedRevision==1 && state.sections[1].requestedRevision==1);
    require(stale(inFlight,state,0));require(*snapshot.find(0)==MissingLayerBoth);
    require(state.sections[0].dirty && state.sections[1].incrementalDirty);
    commit(state,{CorrectionState::Missing,MissingLayerLiquid});require(state.sections[0].requestedRevision==1);
    commit(state,{CorrectionState::Missing,MissingLayerBody});require(state.sections[0].requestedRevision==2);
    commit(state,{CorrectionState::Correct,0});require(state.sections[0].requestedRevision==3);
    commit(state,{CorrectionState::Missing,MissingLayerBoth},false);require(state.sections[0].requestedRevision==3);
    std::vector<std::uint8_t> large(8*1024*1024);
    std::vector<std::size_t> indices;
    for(std::size_t i=0;i<4096;++i) { auto const index=i*2000;indices.push_back(index);large[index]=static_cast<std::uint8_t>(i%4); }
    indices.push_back(indices.back());snapshot.capture(indices,large);
    require(snapshot.size()==4096);require(snapshot.bytes()==4096*(sizeof(std::size_t)+1));
    for(std::size_t i=0;i<4096;++i) { require(*snapshot.find(i*2000)==i%4);require(!snapshot.find(i*2000+1)); }
    large.assign(large.size(),3);require(*snapshot.find(0)==0);
    try { snapshot.capture({large.size()},large);require(false); } catch(std::out_of_range const&) { require(snapshot.size()==4096); }
    snapshot.capture({},large);require(snapshot.size()==0 && snapshot.bytes()==0 && !snapshot.find(0));
    std::cout << "Missing-layer production contracts: " << checks << " checks PASS; native rendering NOT_RUN\n";
}
