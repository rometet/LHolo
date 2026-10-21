"""Compile source-extracted cache contracts with the established private LLVM recipe."""
from pathlib import Path
import datetime, hashlib, json, os, re, subprocess
R=Path(__file__).resolve().parents[1];O=R/'build/missing-layer-tests';G=O/'generated';G.mkdir(parents=True,exist_ok=True)
BASE='b81a4ea089664aef854a65ab68e0045ba62a52ae'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def extract(s,marker):
    a=s.index(marker);b=s.index('{',a);n=1;e=b+1
    while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
    return s[a:e]
def emit(n,s):(G/n).write_text(s+'\n')
cor='src/projection/correction/ProjectionCorrectionTracker.cpp';s=(R/cor).read_text();a=s.index('        auto const& actual =');b=s.index('        auto const nowCorrect =',a)
emit('Classify.inc',s[a:b]);emit('CacheCommit.inc',extract(s,'        if (!visible) return;\n        if (state.correctionStates[index]'))
emit('MarkDirty.inc',extract(s,'void markSectionDirty('))
old=subprocess.check_output(['git','-C',str(R),'show',BASE+':'+cor]).decode();ao=old.index('        auto const& actual =');bo=old.index('        auto const nowCorrect =',ao)
assert old[ao:bo]==s[a:b].split('        auto const nextMissingLayers =')[0], 'Overall correction classification changed'
upload=(R/'src/projection/mesh/ProjectionMeshUpload.cpp').read_text();condition=re.search(r'if \((result.revision != state.sections\[section\].requestedRevision)\)',upload).group(1);emit('Revision.inc','return '+condition+';')
builder=(R/'src/projection/mesh/ProjectionSectionBuilder.cpp').read_text();assert builder.count('!state.buildLiquidShouldRender(index)')==3
assert builder.count('!state.buildBodyShouldRender(index)')==1
assert '(state.buildMissingLayers(index) & MissingLayerBody) == 0' in builder
renderer=(R/'src/projection/mesh/ProjectionRenderer.cpp').read_text();assert '!state.buildBodyShouldRender(projected.structureIndex)' in renderer
paths=[cor,'src/projection/core/ProjectionState.h','src/projection/core/ProjectionMissingLayers.h','src/projection/mesh/SectionMissingLayerSnapshot.h','src/projection/mesh/ProjectionSectionBuilder.cpp','src/projection/mesh/ProjectionRenderer.cpp','src/projection/mesh/ProjectionMeshScheduler.cpp','src/projection/mesh/ProjectionMeshUpload.cpp']
record={'base':BASE,'source':{p:digest(R/p) for p in paths},'generated':{p.name:digest(p) for p in G.iterdir()},'overall_classifier_byte_identical':True,'native_rendering':'NOT_RUN'}
cl=Path('C:/Users/missp/Documents/Codex/2026-10-04/task-7/tools/LLVM-22.1.8/bin/clang-cl.exe');vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207');sdk=Path('C:/Program Files (x86)/Windows Kits/10');v='10.0.26100.0'
env=dict(os.environ);env['INCLUDE']=';'.join(str(p) for p in [vc/'include']+[sdk/'Include'/v/n for n in ['ucrt','shared','um','winrt']]);env['LIB']=';'.join(str(p) for p in [vc/'lib/x64']+[sdk/'Lib'/v/n/'x64' for n in ['ucrt','um']]);env['PATH']=str(cl.parent)+';'+str(vc/'bin/HostX64/x64')+';'+env['PATH'];temp=O/'temp';temp.mkdir(exist_ok=True);env['TEMP']=env['TMP']=str(temp)
exe=O/'MissingLayerTests.exe';args=[str(cl),'/nologo','/std:c++20','/EHsc','/O2','/W4','/WX','/MD','/utf-8','/I'+str(R/'src'),'/I'+str(G),'/Fe:'+str(exe),str(R/'tests/missing_layers/MissingLayerTests.cpp')]
result=subprocess.run(args,cwd=O,env=env,capture_output=True);(O/'build.log').write_bytes(result.stdout+result.stderr);record.update(argv=args,INCLUDE=env['INCLUDE'],LIB=env['LIB'],TEMP=env['TEMP'],compiler_sha256=digest(cl),build_exit=result.returncode)
if result.returncode:print((result.stdout+result.stderr).decode(errors='replace'));result.check_returncode()
result=subprocess.run([str(exe)],cwd=O,env=env,capture_output=True);(O/'run.log').write_bytes(result.stdout+result.stderr);record.update(run_exit=result.returncode,exe_sha256=digest(exe));(O/'TEST_MANIFEST.json').write_text(json.dumps(record,indent=2)+'\n');print(result.stdout.decode());result.check_returncode()
