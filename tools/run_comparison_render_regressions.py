"""Verbatim comparison mesh/submission bodies with explicit Tessellator/material doubles.

Native engine/device behavior, shader appearance and ABI remain real-game checks.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=ROOT/'build/comparison-render-regressions')
args = parser.parse_args(); args.output=args.output.resolve(); args.output.mkdir(parents=True,exist_ok=True)
msvc=sorted(Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC').iterdir())[-1]
sdk=Path('C:/Program Files (x86)/Windows Kits/10'); version=sorted((sdk/'Include').iterdir())[-1].name
env=os.environ.copy(); env['PATH']=str(msvc/'bin/Hostx64/x64')+os.pathsep+env['PATH']
env['INCLUDE']=os.pathsep.join(map(str,[ROOT/'src',msvc/'include',sdk/'Include'/version/'ucrt']))
env['LIB']=os.pathsep.join(map(str,[msvc/'lib/x64',sdk/'Lib'/version/'ucrt/x64',sdk/'Lib'/version/'um/x64']))
builder='src/projection/mesh/ProjectionSectionBuilder.cpp'
renderer='src/projection/mesh/ProjectionRenderer.cpp'
def extract(path,signature):
 source=(ROOT/path).read_text(encoding='utf-8'); start=source.rindex(signature); opening=source.index('{',start)
 depth,end=1,opening+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]

common=r'''
#include "projection/core/ComparisonStyle.h"
#include "app/ScopeExit.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>
#include <vector>
using namespace lholo;
using namespace lholo::projection::detail;
unsigned checks{};
void require(bool condition){++checks;if(!condition){std::fprintf(stderr,"FAIL %u\n",checks);std::exit(1);}}
struct Vec3 {float x,y,z;}; struct BlockPos {int x,y,z;};
namespace mce {
enum class PrimitiveMode {LineList,QuadList,TriangleList};
struct RenderMaterial {PrimitiveMode mPrimitiveMode{PrimitiveMode::LineList};bool depthTest{true};};
struct MaterialPtr {RenderMaterial* value;bool exists{true};};
struct Data {PrimitiveMode mode;std::vector<Vec3> vertices;std::vector<std::uint32_t> colors;};
struct Count {std::optional<unsigned> value;Count(unsigned n):value(n){}auto const& get()const{return value;}};
std::vector<PrimitiveMode> submitted;
struct Mesh {
 Data data;PrimitiveMode mPrimitiveMode;Count mVertexCount;bool throws{};
 Mesh(Data d):data(std::move(d)),mPrimitiveMode(data.mode),mVertexCount(static_cast<unsigned>(data.vertices.size())){}
 bool isValid() const {return true;}
 void renderMesh(int,MaterialPtr const& material,int,int,unsigned,int,std::nullptr_t) const {
  submitted.push_back(material.value?material.value->mPrimitiveMode:PrimitiveMode::LineList);
  if(throws) throw 7;
 }
};
}
struct SupplementaryFieldAutoGenerationMode {int value;};
struct Tessellator {
 struct DebugContextCallback {};enum class UploadMode {Never,Buffered};
 mce::Data data{};std::uint32_t colorValue{};
 void begin(DebugContextCallback,mce::PrimitiveMode mode,int reserve,bool){data={};data.mode=mode;data.vertices.reserve(reserve);data.colors.reserve(reserve);}
 void color(float r,float g,float b,float a){colorValue=static_cast<std::uint32_t>(std::lround(r*255))|(static_cast<std::uint32_t>(std::lround(g*255))<<8)|(static_cast<std::uint32_t>(std::lround(b*255))<<16)|(static_cast<std::uint32_t>(std::lround(a*255))<<24);}
 void tex2(std::array<float,2> uv){require(uv==std::array<float,2>{.5f,.5f});}
 void vertex(float x,float y,float z){data.vertices.push_back({x,y,z});data.colors.push_back(colorValue);}
 mce::Data end(UploadMode,char const*,SupplementaryFieldAutoGenerationMode){return std::move(data);}
};
enum class CorrectionState {Unknown,Missing,Correct,WrongType,WrongState};
struct Entry {BlockPos position;bool block{true};bool liquid{};};
struct Structure {std::vector<Entry> renderBlocks;};
struct ProjectionState {
 std::shared_ptr<Structure> structure=std::make_shared<Structure>();
 BlockPos anchor{};
 std::vector<CorrectionState> correctionStates;
 std::vector<std::vector<std::size_t>> sectionBlockIndices;
 std::vector<std::set<std::tuple<int,int,int>>> sectionExtraBlockPositions;
 std::set<std::tuple<int,int,int>> extraBlockPositions;
 std::shared_ptr<std::map<std::tuple<int,int,int>,std::size_t>> expectedWorldBlockIndices=std::make_shared<std::map<std::tuple<int,int,int>,std::size_t>>();
 std::vector<std::unique_ptr<mce::Mesh>> warningFillSectionMeshes,correctionOutlineSectionMeshes,wrongFillSectionMeshes,wrongOutlineSectionMeshes;
 CorrectionState buildCorrectionState(std::size_t index) const{return correctionStates.at(index);}
 bool hasBuildBlockState(std::size_t index) const{return index<correctionStates.size();}
};
struct ProjectionSectionBuildSettings {int mirrorMode{},rotationTurns{},offsetX{},offsetY{},offsetZ{};float correctionFillOpacity{.15f},correctionOutlineOpacity{1.f},comparisonStrength{1.f},correctionOutlineWidth{1.f};};
BlockPos transformStructurePosition(Entry const& e,Structure const&,int,int){return e.position;}
BlockPos transformStructurePosition(BlockPos p,Structure const&,int,int){return p;}
BlockPos inverseTransformStructurePosition(BlockPos p,Structure const&,int,int){return p;}
constexpr std::uint32_t MissingColorAbgrRgb=0x00E6B333U,ExtraColorAbgrRgb=0x00E64CFFU,WrongBlockColorAbgrRgb=0x003333FFU,WrongStateColorAbgrRgb=0x001090FFU;
'''
helpers='\n'.join(extract(builder,s) for s in ['void setColorAbgr(','std::uint32_t withAlpha(','int correctionPriority('])
meshbody=extract(builder,'void buildCorrectionSectionMeshes(')
meshtests=r'''
int main(){
 ProjectionState state;Tessellator tessellator;ProjectionSectionBuildSettings settings;
 state.structure->renderBlocks={{{0,0,0}},{{3,0,0}},{{6,0,0}},{{9,0,0},false,true},{{12,0,0}}};
 state.correctionStates={CorrectionState::Missing,CorrectionState::WrongType,CorrectionState::WrongState,CorrectionState::Missing,CorrectionState::Correct};
 state.sectionBlockIndices={{0,1,2,3,4}};state.sectionExtraBlockPositions={{{15,0,0}}};state.extraBlockPositions={{15,0,0}};
 state.warningFillSectionMeshes.resize(1);state.correctionOutlineSectionMeshes.resize(1);state.wrongFillSectionMeshes.resize(1);state.wrongOutlineSectionMeshes.resize(1);
 buildCorrectionSectionMeshes(state,tessellator,0,Tessellator::UploadMode::Never,settings);
 require(state.correctionOutlineSectionMeshes[0]->data.mode==mce::PrimitiveMode::LineList);
 require(state.correctionOutlineSectionMeshes[0]->data.vertices.size()==24 && state.wrongOutlineSectionMeshes[0]->data.vertices.size()==72);
 require(state.warningFillSectionMeshes[0]->data.vertices.size()==24 && state.wrongFillSectionMeshes[0]->data.vertices.size()==72);
 require(state.warningFillSectionMeshes[0]->data.colors.front()==withAlpha(MissingColorAbgrRgb,.15f));
 auto originalFill=state.warningFillSectionMeshes[0]->data.colors.front();
 settings.comparisonStrength=.5f;settings.correctionOutlineWidth=5;
 buildCorrectionSectionMeshes(state,tessellator,0,Tessellator::UploadMode::Never,settings);
 require(state.correctionOutlineSectionMeshes[0]->data.mode==mce::PrimitiveMode::QuadList);
 require(state.correctionOutlineSectionMeshes[0]->data.vertices.size()==288 && state.wrongOutlineSectionMeshes[0]->data.vertices.size()==864);
 require((state.warningFillSectionMeshes[0]->data.colors.front()&0xffffff)==(originalFill&0xffffff));
 require(state.warningFillSectionMeshes[0]->data.colors.front()==withAlpha(MissingColorAbgrRgb,.075f));
 require(state.correctionOutlineSectionMeshes[0]->data.colors.front()==withAlpha(MissingColorAbgrRgb,.5f));
 require(state.wrongOutlineSectionMeshes[0]->data.colors[0]==withAlpha(WrongBlockColorAbgrRgb,.5f));
 require(state.wrongOutlineSectionMeshes[0]->data.colors[288]==withAlpha(WrongStateColorAbgrRgb,.5f));
 require(state.wrongOutlineSectionMeshes[0]->data.colors[576]==withAlpha(ExtraColorAbgrRgb,.5f));
 settings.comparisonStrength=0;
 buildCorrectionSectionMeshes(state,tessellator,0,Tessellator::UploadMode::Never,settings);
 require(!state.warningFillSectionMeshes[0]&&!state.correctionOutlineSectionMeshes[0]&&!state.wrongFillSectionMeshes[0]&&!state.wrongOutlineSectionMeshes[0]);
 settings.comparisonStrength=2;settings.correctionOutlineWidth=1;
 buildCorrectionSectionMeshes(state,tessellator,0,Tessellator::UploadMode::Never,settings);
 require(state.warningFillSectionMeshes[0]->data.colors.front()==withAlpha(MissingColorAbgrRgb,.3f));
 require(state.correctionOutlineSectionMeshes[0]->data.colors.front()==withAlpha(MissingColorAbgrRgb,1));
 std::printf("Verbatim comparison mesh: %u checks PASS (native Tessellator doubled)\n",checks);
}
'''
submissionprefix=r'''
bool materialExists(mce::MaterialPtr const& material){return material.exists;}
mce::RenderMaterial* tryRenderMaterial(mce::MaterialPtr const& material){return material.value;}
int emptyOffscreenCaptureDescription(){return 0;}
struct ScopedNoDepthTest {
 mce::RenderMaterial* material;bool saved{};
 ScopedNoDepthTest(mce::MaterialPtr const& value,bool enabled):material(enabled?value.value:nullptr){if(material){saved=material->depthTest;material->depthTest=false;}}
 ~ScopedNoDepthTest(){if(material)material->depthTest=saved;}
};
int main(){
 struct {int mScreenContext{};} renderContext;
 int overlayTexture{};
'''
submissionbody=extract(renderer,'auto renderOverlayMeshes = [&] (')+';\n'
submissiontests=r'''
 mce::RenderMaterial host;host.mPrimitiveMode=mce::PrimitiveMode::TriangleList;
 mce::MaterialPtr material{&host};
 std::vector<std::unique_ptr<mce::Mesh>> meshes;
 meshes.push_back(std::make_unique<mce::Mesh>(mce::Data{mce::PrimitiveMode::LineList,{},{}}));
 meshes.push_back(std::make_unique<mce::Mesh>(mce::Data{mce::PrimitiveMode::QuadList,{},{}}));
 renderOverlayMeshes(meshes,material,true,true);
 require(mce::submitted==std::vector<mce::PrimitiveMode>{mce::PrimitiveMode::LineList,mce::PrimitiveMode::QuadList});
 require(host.mPrimitiveMode==mce::PrimitiveMode::TriangleList && host.depthTest);
 mce::submitted.clear();renderOverlayMeshes(meshes,{nullptr},false,true);
 require(mce::submitted==std::vector<mce::PrimitiveMode>{mce::PrimitiveMode::LineList});
 mce::submitted.clear();meshes[1]->throws=true;bool caught{};
 try{renderOverlayMeshes(meshes,material,true,true);}catch(int){caught=true;}
 require(caught && host.mPrimitiveMode==mce::PrimitiveMode::TriangleList && host.depthTest);
 std::printf("Verbatim comparison submit: %u checks PASS (native material/device doubled)\n",checks);
}
'''
cases={'ComparisonMesh':common+helpers+meshbody+meshtests,
       'ComparisonSubmit':common+submissionprefix+submissionbody+submissiontests}
results=[]
for name,code in cases.items():
 cpp=args.output/(name+'.cpp');cpp.write_text(code,encoding='utf-8');exe=args.output/(name+'.exe')
 build=subprocess.run([str(msvc/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++20','/EHsc','/W4','/WX',str(cpp),'/Fe:'+str(exe),'/Fo:'+str(args.output/(name+'.obj'))],cwd=args.output,env=env,capture_output=True,text=True,errors='replace')
 (args.output/(name+'-build.log')).write_text(build.stdout+build.stderr,encoding='utf-8')
 run=None if build.returncode else subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
 output=build.stdout+build.stderr if run is None else run.stdout+run.stderr
 (args.output/(name+'-run.log')).write_text(output,encoding='utf-8')
 results.append(dict(name=name,buildExit=build.returncode,runExit=None if run is None else run.returncode,generatedSha256=hashlib.sha256(cpp.read_bytes()).hexdigest()))
 print(name,output.strip() if run is not None else output[:1200])
(args.output/'results.json').write_text(json.dumps(dict(results=results,sourceHashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in [builder,renderer,'src/projection/core/ComparisonStyle.h']},scope='Verbatim mesh/submission bodies; explicit native Tessellator/material/device doubles. Does not certify native shader/runtime ABI.'),indent=2),encoding='utf-8')
raise SystemExit(0 if all(r['buildExit']==0 and r['runExit']==0 for r in results) else 1)
