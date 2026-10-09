from pathlib import Path
import hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
from analyze_geometry_probe import analyze,overlap,same
R=Path(__file__).resolve().parents[1];O=R/'build/geometry-probe-tests';O.mkdir(parents=True,exist_ok=True)
BASE='b81a4ea089664aef854a65ab68e0045ba62a52ae'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
builder='src/projection/mesh/ProjectionSectionBuilder.cpp';source=(R/builder).read_text()
stripped=re.sub(r'^#ifdef LHOLO_WATERLOGGED_GEOMETRY_PROBE\n[\s\S]*?^#endif\n','',source,flags=re.M)
base=subprocess.check_output(['git','-C',str(R),'show',BASE+':'+builder]).decode('utf-8-sig');assert stripped==base,'Probe changes non-diagnostic builder code'
assert (R/'src/projection/mesh/ProjectionRenderer.cpp').read_text()==subprocess.check_output(['git','-C',str(R),'show',BASE+':src/projection/mesh/ProjectionRenderer.cpp']).decode('utf-8-sig')
sdk=R.parent/'geometry-probe-offline/packages/l/levilamina/26.51.5/695f712674c7478cbf7fe903bf70afd2/include/mc/deps/core_graphics/enums/PrimitiveMode.h';assert re.search(r'QuadList\s*=\s*1',sdk.read_text())
cl=Path('C:/Users/missp/Documents/Codex/2026-10-04/task-7/tools/LLVM-22.1.8/bin/clang-cl.exe');vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');sdkroot=Path('C:/Program Files (x86)/Windows Kits/10');v='10.0.26100.0'
env=dict(os.environ);env['INCLUDE']=';'.join(str(p) for p in [vc/'include']+[sdkroot/'Include'/v/n for n in ['ucrt','shared','um','winrt']]);env['LIB']=';'.join(str(p) for p in [vc/'lib/x64']+[sdkroot/'Lib'/v/n/'x64' for n in ['ucrt','um']]);env['PATH']=str(cl.parent)+';'+str(vc/'bin/HostX64/x64')+';'+env['PATH'];temp=O/'temp';temp.mkdir(exist_ok=True);env['TEMP']=env['TMP']=str(temp)
exe=O/'GeometryProbeTests.exe';args=[str(cl),'/nologo','/std:c++20','/EHsc','/O2','/W4','/WX','/MD','/utf-8','/I'+str(R/'src'),'/Fe:'+str(exe),str(R/'tests/geometry_probe/GeometryProbeTests.cpp')]
result=subprocess.run(args,cwd=O,env=env,capture_output=True);(O/'build.log').write_bytes(result.stdout+result.stderr)
if result.returncode:print((result.stdout+result.stderr).decode(errors='replace'));result.check_returncode()
fixtures=sorted((R.parent/'geometry-probe-evidence/native-palette-fixtures').glob('*.mcstructure'));assert len(fixtures)==2
result=subprocess.run([str(exe)]+[str(p) for p in fixtures],cwd=O,env=env,capture_output=True);(O/'run.log').write_bytes(result.stdout+result.stderr);result.check_returncode();text=result.stdout.decode();report=analyze(text)
records=[json.loads(line.split('PRAXIS_GEOMETRY_CAPTURE ',1)[1]) for line in text.splitlines() if 'PRAXIS_GEOMETRY_CAPTURE ' in line]
assert len(records)==7 and records[-3]['recorded']==256 and records[-3]['total']==300
assert records[-2]['aligned']==0 and records[-2]['recorded']==256 and records[-2]['vertices'][0][3:5]==[None,None]
assert records[-1]['vertices'][0][0] is None
wet=next(c for c in report['findings'] if c['cell']==0);dry=next(c for c in report['findings'] if c['cell']==1)
assert len(wet['matches'])==1 and wet['matches'][0]['same_positions_uv'] and wet['matches'][0]['liquid_found_in_final']
assert not dry['matches'];q=wet['matches'][0]['body_quad'];partial=[v[:] for v in q]
for p in partial:p[0]=10+(p[0]-10)*.5
assert abs(overlap(q,partial)-.5)<1e-6 and not same(q,partial)
parallel=[v[:] for v in q]
for p in parallel:p[1]+=.25
assert not overlap(q,parallel);assert same(q,list(reversed(q)),True)
different=[v[:] for v in q]
for p in different:p[3]+=.25
assert same(q,different) and not same(q,different,True)
report['input_kind']='SYNTHETIC_CPU_FIXTURE';report['native_game']='NOT_RUN';(O/'SYNTHETIC_ANALYSIS.json').write_text(json.dumps(report,indent=2)+'\n')
manifest={'base':BASE,'source':{p:digest(R/p) for p in [builder,'src/projection/mesh/NativeGeometryProbe.h','src/projection/mesh/ProjectionRenderer.cpp','xmake.lua','tests/geometry_probe/GeometryProbeTests.cpp','tools/analyze_geometry_probe.py','tools/run_geometry_probe_tests.py','tools/build_waterlogged_fixture.py','src/structure/formats/BedrockNbtScanner.h']},'argv':args,'INCLUDE':env['INCLUDE'],'LIB':env['LIB'],'TEMP':env['TEMP'],'compiler_sha256':digest(cl),'sdk_enum_sha256':digest(sdk),'build_exit':0,'run_exit':0,'exe_sha256':digest(exe),'normal_builder_code_byte_identical':True,'renderer_byte_identical':True,'native_game':'NOT_RUN','checks':'capture bounds/immutable inputs/exception scope; parser JSON; full/partial/copanar UV/dry/final/invalid records','native_palette_fixture_NBT_scan':{p.name:digest(p) for p in fixtures}}
(O/'TEST_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n');print('Geometry probe capture/parser/source contracts PASS; synthetic CPU fixture; native game NOT_RUN')
