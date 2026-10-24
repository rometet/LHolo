from pathlib import Path
import subprocess,json,hashlib
import argparse
parser=argparse.ArgumentParser(description='Compile and exercise the actual local placement receiver body against SDK boundary doubles.')
parser.add_argument('--out', type=Path, required=True)
root=parser.parse_args().out.resolve()
root.mkdir(parents=True, exist_ok=True)
repo=Path(__file__).resolve().parents[1]
src=repo/'src/place/LocalPlacementRotation.cpp'
out=root/'receiver-tests';out.mkdir(exist_ok=True)
s=src.read_text(encoding='utf-8');body=s[s.index('namespace lholo::place::detail {'):]
prefix=r'''
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <stdexcept>
#include <cstdint>
namespace lholo::place::detail {void cancelLocalPlacementRotations(std::uint64_t=0,std::uint64_t=0) noexcept;}
std::uint64_t clockMs=1000;unsigned long long GetTickCount64(){return clockMs;}
struct Vec3{float x{},y{},z{};};struct Vec2{float x{},y{};bool operator==(Vec2 const&)const=default;};
struct BlockPos{int x{},y{},z{};bool operator==(BlockPos const&)const=default;};
template<class T>struct Field{T v{};T const& get()const{return v;}T& get(){return v;}Field& operator=(T const& n){v=n;return *this;}};
struct ItemStack{int id=2,aux=2,mCount=64;bool isNull()const{return !mCount;}int getId()const{return id;}int getIdAux()const{return aux;}};
struct Inventory{ItemStack item;ItemStack const& getItem(int)const{return item;}};
struct Block{std::string toDebugString()const{return "fixture";}};struct BlockSource{Block getBlock(BlockPos)const{return {};}};
struct Player{bool client{};std::uint64_t rid=88;int slot=0,dim=0;Vec2 rot{3,7};Inventory inv;BlockSource region;
 bool isClientSide()const{return client;}std::uint64_t getRuntimeID()const{return rid;}int getSelectedItemSlot()const{return slot;}int getDimensionId()const{return dim;}
 Inventory const& getInventory()const{return inv;}Vec2 const& getRotation()const{return rot;}BlockSource const& getDimensionBlockSource()const{return region;}};
struct LocalPlayer:Player{LocalPlayer(){client=true;}};
enum class HandSlot{Mainhand,Offhand};enum class InventoryTransactionError{NoError};
struct ItemUseInventoryTransaction{enum class ActionType{Place,Use};ActionType mActionType=ActionType::Place;HandSlot mHand=HandSlot::Mainhand;int mSlot=0;
 Field<BlockPos> mPos{{-1,0,0}};unsigned char mFace=5;unsigned mTargetBlockId=12;Field<Vec3> mClickPos{{1,.5,.5}},mFromPos{{1,2,3}};};
namespace app::hook_lifecycle{struct DetourGuard{operator bool()const{return true;}};}
#define LL_TYPE_INSTANCE_HOOK(Name,Priority,Type,Id,Ret,...) struct Name:Type{Ret origin(__VA_ARGS__);Ret run(__VA_ARGS__);static int hook(){return 0;}static bool unhook(){return true;}};Ret Name::run(__VA_ARGS__)
'''
main=r'''
using namespace lholo::place::detail;
int calls=0;float seen=0;bool throwOrigin=false;
namespace lholo::place::detail{namespace{
InventoryTransactionError LocalPlacementUseRotationHook::origin(Player& p,bool){++calls;seen=p.getRotation().y;if(throwOrigin)throw std::runtime_error("native failure");return InventoryTransactionError::NoError;}
}}
int checks=0,failures=0;void ck(bool yes,char const* name){++checks;if(!yes){++failures;std::printf("FAIL %s\n",name);}}
int main(){
 LocalPlayer client;Player server;LocalPlacementUseRotationHook tx;
 auto reset=[&]{cancelLocalPlacementRotations();clockMs=1000;client=LocalPlayer{};server=Player{};tx=LocalPlacementUseRotationHook{};calls=0;seen=0;throwOrigin=false;installLocalPlacementRotationHook();};
 auto publish=[&](float yaw){return rememberLocalPlacementRotation(client,{0,0,0},tx,20,yaw,44,6);};
 for(float yaw:{0.F,90.F,180.F,-90.F}){reset();ck(publish(yaw),"queue receipt");tx.run(server,false);ck(calls==1&&seen==yaw,"vanilla receiver sees requested orientation");ck(server.rot==Vec2{3,7},"server rotation restored");ck(client.rot==Vec2{3,7},"client camera unchanged");tx.run(server,false);ck(calls==2&&seen==7,"receipt consumed once; unrelated replay not transformed");}
 reset();publish(90);tx.run(client,false);ck(seen==7&&client.rot==Vec2{3,7},"client actor never transformed");tx.run(server,false);ck(seen==90,"client callback does not consume server receipt");
 reset();publish(90);server.rid=99;tx.run(server,false);ck(seen==7,"different player unchanged");server.rid=88;tx.run(server,false);ck(seen==90,"wrong player did not consume receipt");
 reset();publish(90);server.dim=1;tx.run(server,false);ck(seen==7,"different dimension unchanged");
 reset();publish(90);tx.mHand=HandSlot::Offhand;tx.run(server,false);ck(seen==7,"offhand unchanged");
 reset();publish(90);tx.mActionType=ItemUseInventoryTransaction::ActionType::Use;tx.run(server,false);ck(seen==7,"non-placement unchanged");
 reset();publish(90);tx.mPos.v.x=10;tx.run(server,false);ck(seen==7,"different target unchanged");
 reset();publish(90);tx.mFace=4;tx.run(server,false);ck(seen==7,"different face unchanged");
 reset();publish(90);tx.mTargetBlockId=13;tx.run(server,false);ck(seen==7,"changed support runtime id rejected");
 reset();publish(90);tx.mClickPos.v.y=.75;tx.run(server,false);ck(seen==7,"different click unchanged");
 reset();publish(90);server.inv.item.mCount=63;tx.run(server,false);ck(seen==7,"changed stack unchanged");
 reset();publish(90);server.inv.item.id=3;tx.run(server,false);ck(seen==7,"different item unchanged");
 reset();publish(90);clockMs=1250;tx.run(server,false);ck(seen==7,"expired receipt rejected");
 reset();publish(90);cancelLocalPlacementRotations(44,6);tx.run(server,false);ck(seen==7,"matching cancel prevents transform");
 reset();publish(90);cancelLocalPlacementRotations(45,6);tx.run(server,false);ck(seen==90,"other owner cannot cancel");
 reset();publish(90);throwOrigin=true;try{tx.run(server,false);}catch(std::runtime_error const&){}ck(calls==1,"throw does not replay origin");ck(server.rot==Vec2{3,7},"exception restores server rotation");
 reset();for(int i=0;i<8;++i){tx.mPos.v.x=i;ck(publish(90),"bounded receipt slot admitted");}tx.mPos.v.x=99;ck(!publish(90),"full receipt queue rejects rather than overwriting");clockMs=1250;ck(publish(90),"expired capacity reclaimed");
 reset();ck(!publish(NAN),"nonfinite yaw rejected");uninstallLocalPlacementRotationHook();ck(!publish(90),"disabled receiver rejects publication");
 std::printf("LOCAL_RECEIVER_ACTUAL_BODY checks=%d failures=%d; SDK boundary doubles; live result separate\n",checks,failures);return failures?1:0;
}
'''
(out/'test.cpp').write_text(prefix+body+main,encoding='utf-8')
cmd=f'''@echo off
call "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul
cd /d "{out}"
"E:\\rufu client\\.tools\\llvm-22.1.8\\bin\\clang-cl.exe" /nologo /std:c++20 /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS test.cpp /Fetest.exe
if errorlevel 1 exit /b 2
test.exe
exit /b %errorlevel%
'''
(out/'run.cmd').write_text(cmd)
r=subprocess.run(['cmd','/c',str(out/'run.cmd')],capture_output=True,cwd=out)
(out/'result.log').write_bytes(r.stdout+r.stderr)
print((r.stdout+r.stderr).decode('utf-8',errors='replace'))
(out/'result.json').write_text(json.dumps({'exit':r.returncode,'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'scope':'actual server hook body; native boundaries stubbed'},indent=2))
raise SystemExit(r.returncode)
