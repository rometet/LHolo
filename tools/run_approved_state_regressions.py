"""RA01/RA06 regression checks against verbatim production function bodies.

Minecraft registry/world/session boundaries are explicit test doubles. This is
not a native game test. No xmake, network, shared caches or live files are used.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=ROOT)
parser.add_argument('--output', type=Path, default=ROOT / 'build/approved-regressions')
args = parser.parse_args()
args.source_root = args.source_root.resolve()
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
vs = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC')
msvc = sorted(vs.iterdir())[-1]
sdk = Path('C:/Program Files (x86)/Windows Kits/10')
version = sorted((sdk / 'Include').iterdir())[-1].name
env = os.environ.copy()
env['PATH'] = str(msvc / 'bin/Hostx64/x64') + os.pathsep + env['PATH']
env['INCLUDE'] = str(msvc / 'include') + os.pathsep + str(sdk / 'Include' / version / 'ucrt')
env['LIB'] = os.pathsep.join(map(str, [msvc / 'lib/x64', sdk / 'Lib' / version / 'ucrt/x64', sdk / 'Lib' / version / 'um/x64']))

def function(path, signature):
    text = (args.source_root / path).read_text(encoding='utf-8')
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

COMMON = r'''
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
unsigned checks{};
void require(bool b, char const* label) { ++checks; if (!b) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); } }
'''
FRESHNESS_PREFIX = r'''
struct Block {}; struct BlockPos {}; struct Vec3 {}; struct Level {}; struct Dimension {};
struct LocalPlayer { Level level; Dimension dimension; Level& getLevel(){return level;} Dimension& getDimension(){return dimension;} };
namespace lholo::structure {
struct LoadedStructure { std::uint64_t generation{}; };
std::shared_ptr<LoadedStructure const> published;
auto getLoaded(){return published;}
namespace detail {
struct StructureSession {
    bool shown{true};
    static StructureSession& getInstance(){static StructureSession s; return s;}
    bool visible() const {return shown;}
    auto loaded() const {return published;}
};
}
}
namespace lholo::overlay { struct BoundsWireframe {}; }
namespace lholo::projection {
struct ProjectionQuery { Block const* block; bool missing; };
struct RangeCandidate { Block const* block; };
struct BrokenProjectionCell { int x{}; };
namespace detail {
struct ProjectionState {
    bool enabled{true}, placementCoordinatesInvalid{}, placementBuildActive{};
    std::shared_ptr<structure::LoadedStructure const> structure;
    std::uint64_t structureGeneration{};
    Level* level{}; Dimension* dimension{}; Block oldBlock;
    std::vector<BrokenProjectionCell> pendingBrokenCells{{1}};
};
bool worldExit{}, destroyed{}, eventsFailed{};
bool consumeWorldExitRequest(){return worldExit;}
bool projectionDimensionSourceDestroyed(){return destroyed;}
bool projectionWorldEventsFailed(){return eventsFailed;}
std::recursive_mutex& projectionWorldLifecycleMutex(){static std::recursive_mutex m; return m;}
ProjectionQuery queryProjectionCell(ProjectionState const& s, BlockPos const&){return {&s.oldBlock,true};}
std::vector<RangeCandidate> queryMissingProjectionCells(ProjectionState const& s, Vec3 const&, float){return {{&s.oldBlock}};}
struct ProjectionSession {
    ProjectionState state; overlay::BoundsWireframe bounds; std::mutex mutex;
    static ProjectionSession& getInstance(){static ProjectionSession s; return s;}
    template<class Fn> auto withLockedState(Fn fn){std::lock_guard lock(mutex); return fn(state,bounds);}
};
}
'''
FRESHNESS_TEST = r'''
}
int main(){
    using namespace lholo::projection;
    LocalPlayer p; auto& s=detail::ProjectionSession::getInstance().state;
    s.level=&p.level; s.dimension=&p.dimension;
    auto old=std::make_shared<lholo::structure::LoadedStructure>(); old->generation=1;
    auto next=std::make_shared<lholo::structure::LoadedStructure>(); next->generation=2;
    s.structure=old; s.structureGeneration=1; lholo::structure::published=old;
    require(queryProjection(p,{}).block==&s.oldBlock,"matching load allows cell query");
    require(queryMissingCellsInRange(p,{},1).size()==1,"matching load allows range query");
    // Both normal and restored loads publish before the opaque frame adopts them.
    lholo::structure::published=next;
    require(!queryProjection(p,{}).block,"normal/restored load handoff rejects old cell");
    require(queryMissingCellsInRange(p,{},1).empty(),"load handoff rejects old range");
    require(takeBrokenProjectionCells(p).empty(),"old broken-cell notification stays retired");
    s.structure=next; s.structureGeneration=2;
    require(queryProjection(p,{}).block==&s.oldBlock,"newly adopted load permits placement");
    require(queryMissingCellsInRange(p,{},1).size()==1,"newly adopted load permits range");
    auto& session=lholo::structure::detail::StructureSession::getInstance();
    session.shown=false;
    require(!queryProjection(p,{}).block && queryMissingCellsInRange(p,{},1).empty(),"hidden placement rejects native queries");
    require(takeBrokenProjectionCells(p).empty(),"hidden placement retains correction notifications");
    session.shown=true;
    lholo::structure::published.reset();
    require(!queryProjection(p,{}).block && queryMissingCellsInRange(p,{},1).empty(),"cleared load rejects retained projection");
    lholo::structure::published=next;
    for(auto flag:{&s.enabled,&s.placementCoordinatesInvalid,&s.placementBuildActive,&detail::worldExit,&detail::destroyed,&detail::eventsFailed}) {
        bool previous=*flag; *flag=flag==&s.enabled ? false : true;
        require(!queryProjection(p,{}).block && queryMissingCellsInRange(p,{},1).empty(),"existing admission guards preserved"); *flag=previous;
    }
    LocalPlayer other;
    require(!queryProjection(other,{}).block,"foreign world rejected");
    std::printf("RA01: %u checks PASS\n",checks);
}
'''
CONVERSION_PREFIX = r'''
struct Block {
    struct Type { struct Material { bool mLiquid{}; } mMaterial; } type;
    std::string name; bool isAir()const{return name=="minecraft:air";}
    auto const& getTypeName()const{return name;} auto const& getBlockType()const{return type;}
};
namespace lholo::structure {
using StatePairs=std::vector<std::pair<std::string,std::string>>;
struct ResolvedJavaBlock { Block const* block{}; Block const* liquid{}; bool mapped{}; bool stateResolutionFailed{}; };
struct Mapping { std::string bedrockName; StatePairs bedrockStates; bool waterlogged{}; };
struct PermutationTable { std::vector<std::pair<StatePairs,Block const*>> permutations; };
std::mutex gCacheMutex;
Block defaultBlock{{},"minecraft:barrel"}, exactBlock{{},"minecraft:barrel"}, water{{{true}},"minecraft:water"};
Mapping fixtureMapping{"minecraft:barrel",{{"facing_direction","3"}},false};
PermutationTable fixtureTable;
bool mappingAvailable{}, defaultAvailable{true}; unsigned defaultLookups{};
Mapping const* findMapping(std::string const&,StatePairs const&,int){return mappingAvailable?&fixtureMapping:nullptr;}
auto const& permutationsFor(std::string const&){return fixtureTable;}
Block const* resolveExactBedrockBlock(std::string const&){++defaultLookups;return defaultAvailable?&defaultBlock:nullptr;}
Block const* waterSource(){return &water;}
'''
CONVERSION_TEST = r'''
}
int main(){
    using namespace lholo::structure;
    StatePairs explicitState{{"facing","south"}};
    auto r=resolveJavaBlockState("minecraft:barrel",explicitState,0);
    require(!r.mapped && !r.block && !r.liquid && r.stateResolutionFailed && !defaultLookups,"mapping miss propagates explicit-state failure without default lookup");
    r=resolveJavaBlockState("minecraft:barrel",{},0);
    require(r.mapped && r.block==&defaultBlock,"no-properties name-only compatibility preserved");
    mappingAvailable=true; defaultLookups=0;
    r=resolveJavaBlockState("minecraft:barrel",explicitState,0);
    require(!r.mapped && !r.block && r.stateResolutionFailed && !defaultLookups,"permutation miss propagates instead of wrong facing success");
    fixtureTable.permutations={{{{"facing_direction","2"}},&defaultBlock},{{{"facing_direction","3"}},&exactBlock}};
    r=resolveJavaBlockState("minecraft:barrel",explicitState,0);
    require(r.mapped && r.block==&exactBlock,"correct mapped state resolves exactly");
    fixtureMapping.waterlogged=true;
    r=resolveJavaBlockState("minecraft:barrel",explicitState,0);
    require(r.mapped && r.block==&exactBlock && r.liquid==&water,"successful waterlogged state preserved");
    fixtureTable.permutations.clear();
    r=resolveJavaBlockState("minecraft:barrel",explicitState,0);
    require(!r.mapped && !r.block && !r.liquid && r.stateResolutionFailed,"failed waterlogged state does not fabricate liquid success");
    fixtureMapping.bedrockStates.clear();
    r=resolveJavaBlockState("minecraft:barrel",{},0);
    require(r.mapped && r.block==&defaultBlock,"mapping with no requested states accepts registry default");
    defaultAvailable=false;
    require(!resolveJavaBlockState("minecraft:barrel",{},0).mapped,"missing registry default fails");
    fixtureMapping.bedrockName="minecraft:air";
    require(resolveJavaBlockState("minecraft:air",{},0).mapped,"known mapped air remains successful empty cell");
    std::printf("RA06: %u checks PASS\n",checks);
}
'''
projection = 'src/projection/Projection.cpp'
conversion = 'src/structure/java_to_bedrock/JavaToBedrock.cpp'
loader = 'src/structure/formats/StructureFormatLoaders.cpp'
loader_source = (args.source_root / loader).read_text(encoding='utf-8')
palette_start = loader_source.index('        region.palette.reserve(palette->size());')
palette_end = loader_source.index('        paletteEntries += palette->size();', palette_start)
palette_code = loader_source[palette_start:palette_end]
LOAD_PREFIX = r'''
struct ResolvedJavaBlock { bool stateResolutionFailed{}; };
ResolvedJavaBlock resolveJavaBlock(ResolvedJavaBlock entry,int){return entry;}
std::shared_ptr<int> loadPalette(std::vector<ResolvedJavaBlock> const& entries,std::string& error,std::vector<ResolvedJavaBlock>& result){
    (void)error;
    struct Region {std::vector<ResolvedJavaBlock> palette;} region;
    auto const* palette=&entries; int regionDataVersion{}; std::string regionName="Fixture";
'''
LOAD_TEST = r'''
    result=region.palette;
    return std::make_shared<int>(1);
}
int main(){
    std::string error;std::vector<ResolvedJavaBlock> result;
    require(!loadPalette({{false},{true}},error,result),"failed state aborts load before publication");
    require(error.find("Fixture")!=std::string::npos && error.find("palette index 1")!=std::string::npos,"state failure supplies region and palette diagnostic");
    error.clear();require(bool(loadPalette({{false},{false}},error,result)) && result.size()==2 && error.empty(),"valid/ordinary skipped entries preserve normal load path");
    std::printf("RA06 caller: %u checks PASS\n",checks);
}
'''
cases = {
    'RA01': COMMON + FRESHNESS_PREFIX + '\n'.join(function(projection,s) for s in [
        'bool projectionWorldViewMatches(', 'std::vector<BrokenProjectionCell> takeBrokenProjectionCells(',
        'ProjectionQuery queryProjection(', 'std::vector<RangeCandidate> queryMissingCellsInRange(']) + FRESHNESS_TEST,
    'RA06': COMMON + CONVERSION_PREFIX + function(conversion,'Block const* resolvePermutation(')
        + function(conversion,'ResolvedJavaBlock resolveJavaBlockState(') + CONVERSION_TEST,
    'RA06Caller': COMMON + LOAD_PREFIX + palette_code + LOAD_TEST,
}
results=[]
for name, code in cases.items():
    cpp=args.output/(name+'.cpp'); cpp.write_text(code,encoding='utf-8')
    exe=args.output/(name+'.exe')
    build=subprocess.run([str(msvc/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++20','/EHsc','/W4','/WX',str(cpp),'/Fe:'+str(exe),'/Fo:'+str(args.output/(name+'.obj'))],cwd=args.output,env=env,capture_output=True,text=True,errors='replace')
    (args.output/(name+'-build.log')).write_text(build.stdout+build.stderr,encoding='utf-8')
    run=None if build.returncode else subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
    output=build.stdout+build.stderr if run is None else run.stdout+run.stderr
    (args.output/(name+'.log')).write_text(output,encoding='utf-8')
    result=dict(name=name,buildExit=build.returncode,runExit=None if run is None else run.returncode,generatedSha256=hashlib.sha256(cpp.read_bytes()).hexdigest())
    results.append(result); print(name,output.strip(),flush=True)
(args.output/'results.json').write_text(json.dumps(dict(sourceRoot=str(args.source_root),sources={p:hashlib.sha256((args.source_root/p).read_bytes()).hexdigest() for p in [projection,conversion,loader]},results=results),indent=2),encoding='utf-8')
raise SystemExit(0 if all(r['buildExit']==0 and r['runExit']==0 for r in results) else 1)
