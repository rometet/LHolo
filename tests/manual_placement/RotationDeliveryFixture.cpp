// Actual queue/validation/hook body; SDK/native packet boundaries are doubles.
#include "place/PlacementState.h"
#include "place/PlacementRotationDelivery.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>
using uchar=unsigned char;
struct Vec2 { float x{},y{};bool operator==(Vec2 const&) const=default; };
class BlockPos { public:int x{},y{},z{};bool operator==(BlockPos const&) const=default;bool operator<(BlockPos const& b) const {return std::tie(x,y,z)<std::tie(b.x,b.y,b.z);} };
template<class T> struct Field {T value{};Field()=default;Field(T v):value(std::move(v)) {} T& get(){return value;} T const& get() const {return value;} };
template<class T> struct Field<std::unique_ptr<T>>:std::unique_ptr<T> {using std::unique_ptr<T>::operator=;};
class ItemStack {public:int key{},mCount{},net{};std::string getTypeName()const{return "minecraft:oak_trapdoor";}bool isNull() const{return mCount==0;}int getIdAux() const{return key;}bool matchesNetIdVariant(ItemStack const& other) const{return net==other.net;} };
struct Block {unsigned mNetworkId{};bool air=true;bool isAir()const{return air;} };
struct Region {std::map<BlockPos,Block> cells;Block empty{};Block const& getBlock(BlockPos const& p)const {auto it=cells.find(p);return it==cells.end()?empty:it->second;} };
struct Inventory {std::array<ItemStack,9> items{};ItemStack const& getItem(int slot)const{return items.at(slot);} };
class LocalPlayer {public:Inventory inventory;Region region;Vec2 rotation{17,90};int selected{};Vec2 const& getRotation()const{return rotation;}int getSelectedItemSlot()const{return selected;}Inventory& getInventory(){return inventory;}Region& getDimensionBlockSource(){return region;} };
class ComplexInventoryTransaction {public:virtual ~ComplexInventoryTransaction()=default;};
struct ItemUseInventoryTransaction:ComplexInventoryTransaction {BlockPos mPos{-1,0,0};unsigned mTargetBlockId=12;int slot{},net=101;uchar face=5;};
struct InventoryTransactionPacketPayload {
    Field<int> mLegacyRequestId{7};Field<std::vector<int>> mLegacySetItemSlots{{33}};
    Field<std::variant<int,ItemUseInventoryTransaction>> mVariantTransaction;
    InventoryTransactionPacketPayload(std::unique_ptr<ComplexInventoryTransaction> tx,bool){mVariantTransaction.value=*static_cast<ItemUseInventoryTransaction*>(tx.get());}
};
struct PackedItemUseLegacyInventoryTransaction {Field<int> mID;Field<std::vector<int>> mSlots;Field<ItemUseInventoryTransaction> mTransaction;};
enum class MinecraftPacketIds {PlayerAuthInputPacket,Other};
class Packet {public:virtual ~Packet()=default;virtual MinecraftPacketIds getId()const{return MinecraftPacketIds::Other;} };
struct PlayerAuthInputPacketPayload {
    enum class InputData {PerformItemInteraction,PerformItemStackRequest,PerformBlockActions,Up};
    struct Flags {std::set<InputData> values;bool contains(InputData d)const{return values.contains(d);}void insert(InputData d){values.insert(d);}bool operator==(Flags const&)const=default;};
};
struct PlayerAuthInputPacket:Packet {
    Field<Vec2> mInteractRotation{{17,23}},mRot{{17,90}};float head=91,camera=92;std::uint64_t tick=1234;
    Field<PlayerAuthInputPacketPayload::Flags> mInputData;
    Field<std::unique_ptr<PackedItemUseLegacyInventoryTransaction>> mItemUseTransaction;
    Field<std::unique_ptr<int>> mItemStackRequest;
    MinecraftPacketIds getId()const override{return MinecraftPacketIds::PlayerAuthInputPacket;}
};
class PacketSender {public:virtual ~PacketSender()=default;};
class LoopbackPacketSender:public PacketSender {};
class ClientInstance {public:LocalPlayer* player{};PacketSender* sender{};bool input=true;
    LocalPlayer* getLocalPlayer(){return player;}bool isInGameInputEnabled(){return input;}PacketSender& getPacketSender(){return *sender;}};
ClientInstance client;bool gui{},throwForward{};std::uint64_t clockMs=1000;
std::uint64_t GetTickCount64(){return clockMs;}
namespace ll::service {ClientInstance* getClientInstance(){return &client;}}
namespace lholo::structure {bool isGuiVisible(){return gui;}}
namespace lholo::place::projection {Block ghost{20,false};bool missing=true;struct Query {Block const* block;bool missing;};Query queryProjection(LocalPlayer&,BlockPos const&){return {&ghost,missing};}}
namespace lholo::app {
namespace hook_lifecycle {struct DetourGuard {explicit operator bool()const{return true;}};}
template<class F,class E>bool invokeNativeCallback(F&& f,E&& e){try{f();return true;}catch(...){e("supplied fault");return false;}}
void reportNativeCallbackFailure(char const*,char const*){}
}
struct Forward {bool attached{};Vec2 rotation{},main{};float head{},camera{};std::uint64_t tick{};int id{},slots{},net{};};
std::vector<Forward> forwarded;
namespace lholo::place::detail {namespace {
#include "RotationDeliveryDecls.inc"
struct RotationAuthInputHook:LoopbackPacketSender {
    static int hook(){return 0;}static bool unhook(){return true;}
    void origin(Packet& packet) {
        Forward f;
        if(packet.getId()==MinecraftPacketIds::PlayerAuthInputPacket) {
            auto& input=static_cast<PlayerAuthInputPacket&>(packet);
            f={input.mItemUseTransaction.get()!=nullptr,input.mInteractRotation.get(),input.mRot.get(),input.head,input.camera,input.tick};
            if(f.attached){auto const& p=*input.mItemUseTransaction.get();f.id=p.mID.get();f.slots=p.mSlots.get().at(0);f.net=p.mTransaction.get().net;}
        }
        forwarded.push_back(f);
        if(throwForward)throw std::runtime_error("supplied ambiguous forward failure");
    }
    void run(Packet& packet) {
#include "RotationDeliveryHook.inc"
    }
};
}
#include "RotationDeliveryQueue.inc"
}
int checks{},failures{};
void check(bool v,char const* m){++checks;if(!v){++failures;std::fprintf(stderr,"FAIL %s\n",m);}}
using namespace lholo::place::detail;
int main(){
    LocalPlayer player;RotationAuthInputHook hook;auto& state=PlacementState::getInstance();
    auto reset=[&]{uninstallRotationDeliveryHook();state.resetWorldSession();state.setManualPlacementAllowedItems({});state.setEnabled(true);player=LocalPlayer{};player.inventory.items[0]={1,64,101};player.region.cells[{-1,0,0}]={12,false};client={&player,&hook,true};clockMs=1000;gui=false;throwForward=false;forwarded.clear();lholo::place::projection::ghost={20,false};lholo::place::projection::missing=true;installRotationDeliveryHook();};
    auto queue=[&]{queueRotationPlacement(player,{},0,20,0,player.inventory.items[0],180,std::make_unique<ItemUseInventoryTransaction>());};
    reset();state.setManualMode(true);check(state.beginManualPress(1000,state.manualInputEpoch()),"Manual admission");queue();
    check(state.manualPlaceRequested()&&!state.recentPlacementActive(0,1000),"queue is not send/ack and retains Manual tap");
    PlayerAuthInputPacket input;input.mInputData.get().insert(PlayerAuthInputPacketPayload::InputData::Up);auto flags=input.mInputData.get();hook.run(input);
    check(forwarded.size()==1&&forwarded[0].attached&&forwarded[0].rotation==Vec2{17,180},"one native input carries use plus interaction yaw");
    check(forwarded[0].main==Vec2{17,90}&&forwarded[0].head==91&&forwarded[0].camera==92&&forwarded[0].tick==1234,"main/head/camera/tick stay unchanged");
    check(forwarded[0].id==7&&forwarded[0].slots==33&&forwarded[0].net==101,"native ID/slots/transaction identity preserved");
    check(!input.mItemUseTransaction.get()&&input.mInteractRotation.get()==Vec2{17,23}&&input.mInputData.get()==flags,"packet fields restored after native forwarding");
    check(!state.manualPlaceRequested()&&state.recentPlacementActive(0,1499)&&!state.recentPlacementActive(0,1500),"Manual commit and 500ms lock start at forwarding only");
    hook.run(input);check(forwarded.size()==2&&!forwarded.back().attached,"queued use cannot replay on next native input");
    for(int reason=0;reason<11;++reason){reset();queue();
        switch(reason){case 0:clockMs=1250;break;case 1:state.setRangeEnabled(true);break;case 2:player.selected=1;break;case 3:player.inventory.items[0].net=202;break;case 4:player.inventory.items[0].mCount=63;break;case 5:player.region.cells[{}]={30,false};break;case 6:player.region.cells[{-1,0,0}]={13,false};break;case 7:lholo::place::projection::ghost.mNetworkId=21;break;case 8:gui=true;break;case 9:client.input=false;break;case 10:lholo::place::projection::missing=false;break;}
        PlayerAuthInputPacket packet;hook.run(packet);check(forwarded.size()==1&&!forwarded.back().attached&&!state.recentPlacementActive(0,clockMs),"stale/expired/changed material/support/session/input request rejects before attachment");
    }
    reset();queue();PlayerAuthInputPacket busy;busy.mInputData.get().insert(PlayerAuthInputPacketPayload::InputData::PerformItemInteraction);hook.run(busy);check(!forwarded.back().attached,"native interaction flags are not overwritten");
    clockMs=1050;PlayerAuthInputPacket fresh;hook.run(fresh);check(forwarded.back().attached,"busy packet defers to next fresh native input");
    reset();queue();state.setNextPlaceAt(1040);PlayerAuthInputPacket interval;hook.run(interval);check(!forwarded.back().attached,"legacy and rotation paths share global minimum send interval");
    clockMs=1040;hook.run(interval);check(forwarded.back().attached,"rotation delivery resumes at global interval boundary");
    reset();queue();state.suppressAutoPlacement(0,2000);hook.run(interval);check(!forwarded.back().attached,"queued automatic placement respects new break suppression");
    reset();state.setManualMode(true);check(state.beginManualPress(1000,state.manualInputEpoch()),"deferred Manual tap admitted");state.releaseManualPress();queue();state.setManualPlaceRequested(false);hook.run(interval);check(!forwarded.back().attached,"legacy fulfillment prevents deferred released tap from placing twice");
    reset();state.setManualMode(true);check(state.beginManualPress(1000,state.manualInputEpoch()),"deferred Manual hold admitted");queue();state.setManualPlaceRequested(false);state.setLastManualPlaceAt(1000);clockMs=1040;hook.run(interval);check(!forwarded.back().attached,"deferred held use respects initial and repeat intervals after legacy placement");
    reset();state.setManualMode(true);check(state.beginManualPress(1000,state.manualInputEpoch()),"exempt-item change case admitted");queue();state.setManualPlacementAllowedItems({"minecraft:oak_trapdoor"});hook.run(interval);check(!forwarded.back().attached,"new Manual exempt item preserves vanilla ownership");
    reset();queue();PlayerAuthInputPacket stack;stack.mItemStackRequest=std::make_unique<int>(12);hook.run(stack);check(stack.mItemStackRequest.get()&&*stack.mItemStackRequest.get()==12&&!forwarded.back().attached,"native stack request preserved");
    reset();queue();PlayerAuthInputPacket stackFlag;stackFlag.mInputData.get().insert(PlayerAuthInputPacketPayload::InputData::PerformItemStackRequest);hook.run(stackFlag);check(!forwarded.back().attached,"native stack request flag alone retains native ownership");
    reset();queue();PlayerAuthInputPacket blockFlag;blockFlag.mInputData.get().insert(PlayerAuthInputPacketPayload::InputData::PerformBlockActions);hook.run(blockFlag);check(!forwarded.back().attached,"native block-action flag retains native ownership");
    reset();std::thread other([&]{queue();});other.join();PlayerAuthInputPacket packet;hook.run(packet);check(!forwarded.back().attached,"wrong thread cannot inspect native player or consume request");
    reset();queue();LoopbackPacketSender wrong;client.sender=&wrong;hook.run(packet);check(!forwarded.back().attached,"other sender cannot consume request");
    reset();queue();throwForward=true;hook.run(packet);check(!packet.mItemUseTransaction.get()&&packet.mInteractRotation.get()==Vec2{17,23},"ambiguous native forward fault restores packet fields");
    check(state.recentPlacementActive(0,1499),"ambiguous forwarding attempt also retains 500ms suppression");
    uninstallRotationDeliveryHook();
    std::printf("RotationDeliveryFixture: %d checks, %d failures; native serialization/server authority NOT_RUN\n",checks,failures);
    return failures?1:0;
}
