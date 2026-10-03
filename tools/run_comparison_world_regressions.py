"""Exercise verbatim production style invalidation/world release with explicit native doubles.

No game, native material or device is started. These tests do not certify runtime ABI.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=ROOT / 'build/comparison-regressions')
args = parser.parse_args()
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
msvc = sorted(Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC').iterdir())[-1]
sdk = Path('C:/Program Files (x86)/Windows Kits/10')
version = sorted((sdk / 'Include').iterdir())[-1].name
env = os.environ.copy()
env['PATH'] = str(msvc / 'bin/Hostx64/x64') + os.pathsep + env['PATH']
env['INCLUDE'] = os.pathsep.join(map(str, [ROOT / 'src', msvc / 'include', sdk / 'Include' / version / 'ucrt']))
env['LIB'] = os.pathsep.join(map(str, [msvc / 'lib/x64', sdk / 'Lib' / version / 'ucrt/x64', sdk / 'Lib' / version / 'um/x64']))

def function(path, signature):
    source = (ROOT / path).read_text(encoding='utf-8')
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include "projection/core/ComparisonStyle.h"
#include "projection/runtime/ProjectionInvalidation.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>
unsigned checks{};
void require(bool value) { ++checks; if(!value) {std::fprintf(stderr,"check %u failed\n",checks);std::exit(1);} }
namespace lholo::structure {
struct LoadedStructure { struct RenderBlock {int materialIndex{},liquidMaterialIndex{};}; std::vector<RenderBlock> renderBlocks; };
namespace detail {struct StructureSession {static StructureSession& getInstance(){static StructureSession s;return s;} bool countExtras(){return true;} };}
}
namespace lholo::projection::detail {
enum class CorrectionState {Unknown,Missing,Correct,WrongType,WrongState};
struct SectionState {bool dirty{},incrementalDirty{},buildInFlight{};std::uint64_t requestedRevision{};std::vector<std::unique_ptr<int>> meshes;};
struct ProjectionState {
 std::shared_ptr<structure::LoadedStructure> structure;
 std::vector<SectionState> sections;
 std::vector<std::size_t> blockToSection;
 std::vector<CorrectionState> correctionStates;
 std::vector<int> progressCorrect,progressErrorKind;
 bool meshPreflightDone{},cachedCountExtras{true},praxisCompatLiquidAggregateDirty{};
 int cachedRotation{},cachedMirror{},cachedOffsetX{},cachedOffsetY{},cachedOffsetZ{},cachedDisplayLayer{};
 std::optional<structure::LayerDisplayMode> cachedLayerDisplayMode{structure::LayerDisplayMode::All};
 std::optional<structure::LayerAxis> cachedLayerAxis{structure::LayerAxis::Y};
 float cachedOpacity{1},cachedCorrectionFillOpacity{.15f},cachedCorrectionOutlineOpacity{1},cachedComparisonStrength{1},cachedCorrectionOutlineWidth{1};
 std::vector<int> detectedExtraBlockPositions,extraBlockPositions,praxisCompatLiquidAggregateOrder,praxisCompatLiquidBoundaryMaskCache;
 std::vector<std::vector<int>> sectionExtraBlockPositions;
 std::vector<std::unique_ptr<int>> warningFillSectionMeshes,correctionOutlineSectionMeshes,wrongFillSectionMeshes,wrongOutlineSectionMeshes,nativeLiquidSectionMeshes,praxisCompatLiquidSections,liquidProxySectionMeshes,blockEntityPlaceholderSectionMeshes;
 std::unique_ptr<int> praxisCompatLiquidAggregate,structureBoundsMesh;
 std::vector<std::size_t> nativeLiquidSectionCellCounts,liquidProxySectionCellCounts;
 std::uint64_t progressExtraCount{},progressWrongTypeCount{},progressWrongStateCount{},progressCorrectCount{},progressVisibleCorrectCount{},progressRevision{},extraScanCell{};
 std::size_t extraScanRegion{};
};
int projectionLayer(structure::LoadedStructure const&, structure::LoadedStructure::RenderBlock const&,structure::LayerAxis,int,int){return 0;}
bool isLayerVisible(int,structure::LayerDisplayMode,int,int,int,structure::LayerAxis){return true;}
void publishErrorProgress(std::uint64_t,std::uint64_t,std::uint64_t){}
void resetPublishedBuildProgressCounts(){}
void publishVisibleProgress(std::uint64_t,std::uint64_t){}
enum class ProjectionReleaseScope : unsigned char {Dimension,World};
std::vector<int> events;
void stopMeshWorker(){events.push_back(1);}
void detachProjectionDimensionEvents(){events.push_back(2);}
void detachProjectionWorldEvents(){events.push_back(3);}
void resetPublishedBuildProgress(){events.push_back(4);}
'''
invalidation = 'src/projection/runtime/ProjectionInvalidation.cpp'
lifecycle = 'src/projection/runtime/ProjectionLifecycle.cpp'
body = '\n'.join(function(invalidation, s) for s in [
    'void markSectionDirty(', 'void markAllSectionsDirty(', 'ProjectionInvalidationResult reconcileProjectionInvalidation('])
body += '\n' + '\n'.join(function(lifecycle, s) for s in [
    'void releaseProjectionState(', 'void resetProjectionState(', 'void suspendProjectionState('])
tests = r'''
}
int main(){
 using namespace lholo::projection::detail;
 ComparisonPreferences prefs;
 prefs.setStrength(1.65f);prefs.setOutlineWidth(6.5f);
 ProjectionState state;
 state.structure=std::make_shared<lholo::structure::LoadedStructure>();state.structure->renderBlocks.resize(1);
 state.sections.resize(2);state.blockToSection={0};state.correctionStates={CorrectionState::Missing};state.progressCorrect={0};state.progressErrorKind={1};state.meshPreflightDone=true;
 state.sections[0].requestedRevision=7;state.sections[1].requestedRevision=9;
 ProjectionInvalidationSettings settings;
 settings.structureOpacity=1;settings.correctionFillOpacity=.15f;settings.correctionOutlineOpacity=1;
 auto result=reconcileProjectionInvalidation(state,settings);
 require(!result.placementViewChanged() && !state.sections[0].dirty && state.meshPreflightDone);
 settings.comparisonStrength=prefs.strength();
 result=reconcileProjectionInvalidation(state,settings);
 require(!result.placementViewChanged() && state.sections[0].dirty && state.sections[1].dirty && !state.meshPreflightDone);
 require(state.sections[0].requestedRevision==8 && state.sections[1].requestedRevision==10);
 require(state.cachedComparisonStrength==1.65f && state.progressErrorKind[0]==1);
 state.sections[0].dirty=false;state.sections[1].dirty=false;state.meshPreflightDone=true;
 result=reconcileProjectionInvalidation(state,settings);
 require(!state.sections[0].dirty && !state.sections[1].dirty && state.meshPreflightDone);
 auto staleRevision=state.sections[0].requestedRevision;
 state.sections[0].buildInFlight=true;settings.correctionOutlineWidth=prefs.outlineWidth();
 result=reconcileProjectionInvalidation(state,settings);
 require(!result.placementViewChanged() && state.sections[0].dirty && state.sections[0].requestedRevision==staleRevision+1);
 require(staleRevision!=state.sections[0].requestedRevision && state.cachedCorrectionOutlineWidth==6.5f);
 settings.comparisonStrength=0;
 reconcileProjectionInvalidation(state,settings);
 require(state.cachedComparisonStrength==0 && state.sections[0].dirty);
 suspendProjectionState(state);
 require(events==std::vector<int>({1,2,4}));
 require(!state.structure && state.sections.empty());
 require(prefs.strength()==1.65f && prefs.outlineWidth()==6.5f);
 events.clear();state.structure=std::make_shared<lholo::structure::LoadedStructure>();
 resetProjectionState(state);
 require(events==std::vector<int>({1,3,4}));
 require(!state.structure && prefs.strength()==1.65f && prefs.outlineWidth()==6.5f);
 std::size_t vertices{};
 for(int cell=0;cell<4096;++cell) for(int edge=0;edge<12;++edge)
  emitThickComparisonEdge({0,0,0},{1,0,0},8,[&](ComparisonQuad const& quad){vertices+=quad.size();});
 require(vertices==1179648 && vertices<static_cast<std::size_t>(INT_MAX));
 std::printf("Comparison world/style: %u checks PASS; max section CPU vertices=%zu (native Tessellator/device NOT RUN)\n",checks,vertices);
}
'''
cpp = args.output / 'ComparisonWorld.cpp'
cpp.write_text(prefix+body+tests, encoding='utf-8')
exe = args.output / 'ComparisonWorld.exe'
build = subprocess.run([str(msvc/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++20','/EHsc','/W4','/WX',str(cpp),'/Fe:'+str(exe),'/Fo:'+str(args.output/'ComparisonWorld.obj')], cwd=args.output, env=env, capture_output=True,text=True,errors='replace')
(args.output/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
run = None if build.returncode else subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
output = build.stdout+build.stderr if run is None else run.stdout+run.stderr
(args.output/'run.log').write_text(output,encoding='utf-8')
result = dict(buildExit=build.returncode,runExit=None if run is None else run.returncode,
             sourceHashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in [invalidation,lifecycle,'src/projection/core/ComparisonStyle.h']},
             generatedSha256=hashlib.sha256(cpp.read_bytes()).hexdigest(),
             scope='verbatim production bodies with world/listener/progress/native doubles; no Minecraft/native renderer/runtime ABI test')
(args.output/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(output.strip())
raise SystemExit(0 if run is not None and run.returncode==0 else 1)
