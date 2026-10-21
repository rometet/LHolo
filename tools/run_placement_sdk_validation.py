"""Fresh registered targets; save failed checks and continue unaffected work."""
from pathlib import Path
from datetime import datetime, timezone
import argparse,json,os,shutil,subprocess,time
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);args=p.parse_args()
root=Path(__file__).resolve().parents[1];out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ);offline=root/'.offline';temp=root/'build/test-temp';temp.mkdir(parents=True,exist_ok=True)
env.update(XMAKE_GLOBALDIR=str(offline),XMAKE_PKG_INSTALLDIR=str(offline/'packages'),XMAKE_PKG_CACHEDIR=str(offline/'package-cache'),TEMP=str(temp),TMP=str(temp))
env['PATH']=r'E:\rufu client\.tools\llvm-22.1.8\bin;'+env['PATH']
xmake=shutil.which('xmake.exe');assert xmake
targets=['LHoloLogicTests','LHoloNbtTests','LHoloHudControlApiTests','LHoloLanguageStoreTests','LHoloUiTests','LHoloGraphicsTests','LHoloTranslucencyTests','LHoloVerifierRenderTests','LHoloMaterialsTests','LHoloExportLifecycleTests','LHoloBlockIconTests','LHoloAuditBench','LHoloUiBench','LHolo']
results=[]
for target in targets:
 argv=[xmake,'-b','-j','4',target];start=time.perf_counter()
 r=subprocess.run(argv,cwd=root,env=env,capture_output=True)
 (out/(target+'-build.log')).write_bytes(r.stdout+r.stderr)
 rec={'target':target,'build_exit':r.returncode,'build_seconds':time.perf_counter()-start,'build_argv':argv,'run_exit':None}
 if not r.returncode and target!='LHolo':
  exe=root/'build/placement-candidate/windows/x64/release'/(target+'.exe')
  argv=[str(exe)]+(['--warp-only'] if target=='LHoloGraphicsTests' else [])
  if target=='LHoloExportLifecycleTests':argv.append(str(exe.parent/'LHoloExportDialogFixture.dll'))
  start=time.perf_counter();r=subprocess.run(argv,cwd=root,env=env,capture_output=True)
  (out/(target+'-run.log')).write_bytes(r.stdout+r.stderr)
  rec.update(run_exit=r.returncode,run_seconds=time.perf_counter()-start,run_argv=argv)
 results.append(rec)
 (out/'results.json').write_text(json.dumps({'observed_utc':datetime.now(timezone.utc).isoformat(),'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'scope':'standalone fixture; native Minecraft/BDS NOT_RUN','results':results},indent=2),encoding='utf-8')
 print(f"{target} build={rec['build_exit']} run={rec['run_exit']}",flush=True)
raise SystemExit(1 if any(r['build_exit'] or r['run_exit'] for r in results) else 0)
