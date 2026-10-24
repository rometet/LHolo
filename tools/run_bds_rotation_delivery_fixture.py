"""Exercise production queue/status/dispatch bodies with SDK boundary doubles.
This is an offline regression test; it is not BDS acceptance.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,required=True);args=parser.parse_args()
repo=Path(__file__).resolve().parents[1];out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
s=(repo/'src/place/PlacementRotationDelivery.cpp').read_text(encoding='utf-8-sig')
start=s.index(',Packet& packet) {')+len(',Packet& packet) {');end=s.index('\n}\n}\n\nextern "C" __declspec(dllexport)',start)
exportstart=s.index('extern "C" __declspec(dllexport)',end);exportend=s.index('bool queueRotationPlacement',exportstart)
parts={'Decls':s[s.index('struct PendingRotation'):s.index('LL_TYPE_INSTANCE_HOOK')],'Hook':s[start:end],'Queue':s[exportend:s.rindex('\n}')],'Exports':s[exportstart:exportend]}
for name,text in parts.items():(out/('RotationDelivery'+name+'.inc')).write_text(text,encoding='utf-8')
cl=Path(r'E:\rufu client\.tools\llvm-22.1.8\bin\clang-cl.exe')
vc=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207')
sdk=Path(r'C:\Program Files (x86)\Windows Kits\10');v='10.0.26100.0'
env=dict(os.environ);env['INCLUDE']=';'.join(map(str,[vc/'include']+[sdk/'Include'/v/n for n in ['ucrt','shared','um','winrt']]))
env['LIB']=';'.join(map(str,[vc/'lib/x64',sdk/'Lib'/v/'ucrt/x64',sdk/'Lib'/v/'um/x64']))
env['PATH']=str(cl.parent)+';'+str(vc/'bin/HostX64/x64')+';'+env['PATH']
cmd=[str(cl),'/nologo','/std:c++20','/EHsc','/utf-8','/I'+str(repo/'src'),'/I'+str(out),str(repo/'tests/manual_placement/BdsRotationDeliveryFixture.cpp'),str(repo/'src/place/PlacementState.cpp'),'/Fe'+str(out/'delivery.exe')]
r=subprocess.run(cmd,cwd=out,env=env,capture_output=True);(out/'build.log').write_bytes(r.stdout+r.stderr)
result={'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'build_exit':r.returncode,'boundary':'SDK/network doubles; BDS NOT_RUN'}
if not r.returncode:
 r=subprocess.run([str(out/'delivery.exe')],cwd=out,capture_output=True);(out/'run.log').write_bytes(r.stdout+r.stderr)
 result.update(run_exit=r.returncode,output=r.stdout.decode(errors='replace'))
(out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result));raise SystemExit(r.returncode)
