"""Build only the private candidate; record exact inputs, output hashes and exits."""
from pathlib import Path
from datetime import datetime, timezone
import argparse, hashlib, json, os, shutil, subprocess, time

p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);args=p.parse_args()
repo=Path(__file__).resolve().parents[1];out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
def git(*argv):return subprocess.check_output(['git',*argv],cwd=repo,text=True).strip()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
assert not git('status','--porcelain'),'commit source and harness before final build'
inputs={line.split('\t')[1]:sha(repo/line.split('\t')[1]) for line in git('ls-tree','-r','HEAD','--','src','xmake.lua','manifest.json').splitlines()}
env=dict(os.environ);offline=repo/'.offline'
env.update(XMAKE_GLOBALDIR=str(offline),XMAKE_PKG_INSTALLDIR=str(offline/'packages'),XMAKE_PKG_CACHEDIR=str(offline/'package-cache'))
env['PATH']=r'E:\rufu client\.tools\llvm-22.1.8\bin;'+env['PATH']
argv=[shutil.which('xmake.exe'),'-b','-j','4','LHolo'];assert argv[0]
start=time.perf_counter();r=subprocess.run(argv,cwd=repo,env=env,capture_output=True)
(out/'product-final.log').write_bytes(r.stdout+r.stderr)
record={'observed_utc':datetime.now(timezone.utc).isoformat(),'revision':git('rev-parse','HEAD'),'argv':argv,
        'exit':r.returncode,'seconds':time.perf_counter()-start,'input_sha256':inputs,
        'inputs_unchanged_during_build':all(sha(repo/f)==v for f,v in inputs.items()),
        'native_Minecraft_BDS':'NOT_RUN','runtime_adoption':'HOLD'}
assert record['inputs_unchanged_during_build']
if not r.returncode:
 build=repo/'build/placement-candidate/windows/x64/release'
 record['outputs']={name:{'sha256':sha(build/name),'bytes':(build/name).stat().st_size} for name in ('LHolo.dll','LHolo.pdb')}
(out/'product-final.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in record.items() if k!='input_sha256'},indent=2));raise SystemExit(r.returncode)
