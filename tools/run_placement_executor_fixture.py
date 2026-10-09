"""Compile unchanged production bodies against explicit offline boundary doubles.

No Minecraft, BDS, native prediction implementation, or server acknowledgement
is exercised. Function bodies are extracted verbatim, with their SHA-256 saved.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, time

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args(); src=args.source.resolve(); out=args.out.resolve()
out.mkdir(parents=True,exist_ok=True)
text=(src/'src/place/PlacementExecutor.cpp').read_text(encoding='utf-8')
def section(start,end):
    return text[text.index(start):text.index(end,text.index(start))]
# Boundaries deliberately include comments and exact signatures. Fail closed if
# source movement removes one, rather than silently testing a handwritten copy.
parts=[section('constexpr int kHotbarSlots','std::int64_t packBlockPos'),
       section('std::int64_t packBlockPos','void consumeBrokenProjectionCells'),
       section('struct ItemFind','constexpr uchar kInvalidFace'),
       section('FailedPlanKey makeFailedPlanKey','// Server-synced slot exchange'),
       section('bool placeBlock','// Candidate click points'),
       section('bool placementPredictionMatches','bool resolveOrientedPlacement'),
       section('void tickRangePlaceImpl','void tickEasyPlaceImpl'),
       section('void tickEasyPlaceImpl','} // namespace\n')]
# Actual packet boundary uses the unchanged placeBlock body. The planner is a
# supplied outcome in scheduling tests; prediction state comparison is tested
# separately using the unchanged placementPredictionMatches body.
(out/'ConstantsBody.inc').write_text(parts[0],encoding='utf-8')
(out/'ExecutorBodies.inc').write_text('\n'.join(parts[1:]),encoding='utf-8')
planner_parts=[section('ProjectionTarget selectPlacementTarget','// Voxel raycast'),
               section('template <class F>\nvoid forEachClickCandidate','// Read a serialized Bedrock'),
               section('bool resolveOrientedPlacement','void tickRangePlaceImpl')]
(out/'PlannerBody.inc').write_text('\n'.join(planner_parts),encoding='utf-8')
swap_part=section('// Server-synced slot exchange','// A projected ghost cell')
(out/'SwapBody.inc').write_text(swap_part,encoding='utf-8')
helper=(src/'src/place/PlaceHelper.cpp').read_text(encoding='utf-8')
hook_start=helper.index('GameModeStartBuildHook,')
hook_start=helper.index(') {',hook_start)+3
hook_end=helper.index('// Right-clicking a floating projection',hook_start)
hook_body=helper[hook_start:hook_end].rstrip()
assert hook_body.endswith('}')
hook_body=hook_body[:-1].rstrip()
(out/'StartBuildBody.inc').write_text(hook_body,encoding='utf-8')
identity=(src/'src/block/BlockPlacementRules.h').read_text(encoding='utf-8')
identity=identity[identity.index('[[nodiscard]] inline std::string_view placeableBaseName'):identity.index('// Returns the stable identity')]
(out/'BlockIdentity.inc').write_text(identity,encoding='utf-8')
cl=Path(r'E:\rufu client\.tools\llvm-22.1.8\bin\clang-cl.exe')
vc=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207')
sdk=Path(r'C:\Program Files (x86)\Windows Kits\10');ver='10.0.26100.0'
env=dict(os.environ)
env['INCLUDE']=';'.join(map(str,[vc/'include']+[sdk/'Include'/ver/n for n in ('ucrt','shared','um','winrt')]))
env['LIB']=';'.join(map(str,[vc/'lib/x64']+[sdk/'Lib'/ver/n/'x64' for n in ('ucrt','um')]))
env['PATH']=str(cl.parent)+';'+str(vc/'bin/HostX64/x64')+';'+env['PATH']
env['TEMP']=str(out);env['TMP']=str(out)
fixture=Path(__file__).resolve().parents[1]/'tests/manual_placement/ExecutorFixture.cpp'
argv=[str(cl),'/nologo','/std:c++20','/EHsc','/O2','/W4','/WX','/MD','/utf-8','/I'+str(src/'src'),'/I'+str(Path(__file__).resolve().parents[1]/'src'),'/I'+str(out),'/Fe:'+str(out/'executor-fixture.exe'),str(fixture),str(src/'src/place/PlacementState.cpp')]
t=time.perf_counter();r=subprocess.run(argv,cwd=out,env=env,capture_output=True)
(out/'build.log').write_bytes(r.stdout+r.stderr)
rec={'source':str(src),'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=src,text=True).strip(), 'scope':'offline actual bodies, boundary doubles; NOT native prediction/BDS/server apply', 'body_sha256':[hashlib.sha256(p.encode()).hexdigest() for p in parts], 'planner_body_sha256':[hashlib.sha256(p.encode()).hexdigest() for p in planner_parts], 'swap_body_sha256':hashlib.sha256(swap_part.encode()).hexdigest(),'start_build_body_sha256':hashlib.sha256(hook_body.encode()).hexdigest(),'identity_body_sha256':hashlib.sha256(identity.encode()).hexdigest(), 'argv':argv,'build_exit':r.returncode,'build_seconds':time.perf_counter()-t}
if not r.returncode:
 t=time.perf_counter();r=subprocess.run([str(out/'executor-fixture.exe')],cwd=out,env=env,capture_output=True)
 (out/'run.log').write_bytes(r.stdout+r.stderr)
 rec.update(run_exit=r.returncode,run_seconds=time.perf_counter()-t,stdout=r.stdout.decode(errors='replace'),stderr=r.stderr.decode(errors='replace'),exe_sha256=hashlib.sha256((out/'executor-fixture.exe').read_bytes()).hexdigest())
 coverage=[json.loads(line.removeprefix('COVERAGE ')) for line in rec['stdout'].splitlines() if line.startswith('COVERAGE ')]
 if coverage:
  (out/'coverage.json').write_text(json.dumps(coverage,indent=2),encoding='utf-8');rec['coverage_rows']=len(coverage)
(out/'results.json').write_text(json.dumps(rec,indent=2),encoding='utf-8')
print(json.dumps(rec));raise SystemExit(r.returncode)
