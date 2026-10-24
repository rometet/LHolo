from pathlib import Path
import subprocess,json
import argparse
parser=argparse.ArgumentParser(description='Prove expired external status owners are retired; baseline must demonstrate the original regression.')
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--baseline', default='01fca5b0b50ab0e32617fed2a2caa50e5a6cfb87')
args=parser.parse_args()
root=args.out.resolve()
root.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[1]
baseline=subprocess.check_output(['git','show',args.baseline+':src/place/PlacementRotationDelivery.cpp'],cwd=repo)
(root/'PlacementRotationDelivery.cpp.before').write_bytes(baseline)
files={'before':root/'PlacementRotationDelivery.cpp.before','after':repo/'src/place/PlacementRotationDelivery.cpp'}
def function(s):
 p=s.index('lholo_flexible_placement_v2_get_status() noexcept {')
 start=s.index('{',p); depth=1; end=start+1
 while depth:
  if s[end]=='{': depth+=1
  elif s[end]=='}': depth-=1
  end+=1
 return s[p:end]
prefix=r'''
#include "place/FlexiblePlacementBridge.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
std::uint64_t clockMs=1000;
std::uint64_t GetTickCount64(){return clockMs;}
void traceFlexibleDelivery(char const*, ...) noexcept {}
struct PlacementState {
 bool e{},m{},r{}; std::uint64_t epoch=5;
 bool enabled()const{return e;}bool manualMode()const{return m;}bool rangeEnabled()const{return r;}
 std::uint64_t manualInputEpoch()const{return epoch;}
 static PlacementState& getInstance(){static PlacementState s;return s;}
};
struct Pending {bool external=true;std::uint64_t expires=1250,epoch=5,ownerToken=44,ownerEpoch=6,worldGeneration=7;int dimension=0;};
std::optional<Pending> pending;std::mutex pendingMutex;std::atomic_bool available=true;
lholo::flexible_placement_abi::FlexiblePlacementStatusV2
'''
main=r'''
int checks=0,failures=0;
void ck(bool v,char const* name){++checks;if(!v){++failures;std::printf("FAIL %s\n",name);}}
int main(){
 pending=Pending{};auto live=lholo_flexible_placement_v2_get_status();ck(live.ownerToken==44,"live request kept");
 clockMs=1249;ck(lholo_flexible_placement_v2_get_status().ownerToken==44,"one millisecond before deadline kept");
 clockMs=1250;ck(lholo_flexible_placement_v2_get_status().ownerToken==0,"expired request not reported as competing owner");ck(!pending,"expired payload released without waiting for another queue call");
 clockMs=1000;pending=Pending{};PlacementState::getInstance().epoch=6;ck(lholo_flexible_placement_v2_get_status().ownerToken==0,"retired input epoch not reported busy");ck(!pending,"retired epoch payload released");
 PlacementState::getInstance().epoch=5;pending=Pending{};pending->external=false;clockMs=1400;lholo_flexible_placement_v2_get_status();ck(pending.has_value(),"status query must not cancel LHolo internal request");
 pending.reset();PlacementState::getInstance().e=true;ck(lholo_flexible_placement_v2_get_status().statusBits!=0,"actual LHolo mode still owns placement");
 std::printf("STATUS_EXPIRY checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
'''
results={}
for tag,p in files.items():
 out=root/('status-'+tag);out.mkdir(exist_ok=True)
 (out/'test.cpp').write_text(prefix+function(p.read_text(encoding='utf-8-sig'))+main,encoding='utf-8')
 cmd=f'''@echo off
call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul
cd /d "{out}"
"E:\\rufu client\\.tools\\llvm-22.1.8\\bin\\clang-cl.exe" /nologo /std:c++20 /EHsc /utf-8 /I"{repo/'src'}" test.cpp /Fetest.exe
if errorlevel 1 exit /b 2
test.exe
exit /b %errorlevel%
'''
 (out/'run.cmd').write_text(cmd)
 r=subprocess.run(['cmd','/c',str(out/'run.cmd')],capture_output=True,cwd=out)
 (out/'run.log').write_bytes(r.stdout+r.stderr)
 results[tag]={'exit':r.returncode,'output':(r.stdout+r.stderr).decode('utf-8',errors='replace')}
 print(tag,results[tag])
(root/'STATUS_EXPIRY_RESULTS.json').write_text(json.dumps(results,indent=2))
assert results['before']['exit']==1 and results['after']['exit']==0,results
