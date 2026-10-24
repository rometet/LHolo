// Actual queue/validation/hook body; SDK/native packet boundaries are doubles.
#include "place/PlacementState.h"
#include "place/ExternalRotationDeliveryPolicy.h"
void traceFlexibleDelivery(char const*, ...) noexcept {}
#include "place/PlacementRotationDelivery.h"
#include "place/EasyPlaceTelemetry.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>
using HMODULE=void*;using DWORD=unsigned long;using LPCWSTR=wchar_t const*;using LPCSTR=char const*;using FARPROC=void*;
using uchar=unsigned char;
namespace lholo::flexible_placement_abi {
inline constexpr std::uint32_t kVersion=1;inline constexpr std::uint32_t kVersion2=2;
enum StatusBits : std::uint32_t {EasyPlaceOwnsPlacement=1u<<0,ManualOwnsPlacement=1u<<1,RangeOwnsPlacement=1u<<2};
enum RequestFlags : std::uint32_t {RequestGameplay=1u<<0,RequestFocused=1u<<1,RequestGuiClear=1u<<2,RequestModifiers=1u<<3,RequestAirTarget=1u<<4,RequestRotationSync=1u<<5,RequestRotationMode=1u<<6,RequestOffsetMode=1u<<7,RequestAdjacentMode=1u<<8};
enum QueueResult : std::uint32_t {QueueRejected=0,QueueAccepted=1,QueueBusy=2,QueueUnavailable=3};
enum OwnerStatusBits : std::uint32_t {OwnerFeatureEnabled=1u<<0,OwnerGameplay=1u<<1,OwnerFocused=1u<<2,OwnerGuiClear=1u<<3,OwnerModifiers=1u<<4,OwnerWorldMatch=1u<<5,OwnerDimensionMatch=1u<<6,OwnerStackMatch=1u<<7,OwnerModeMatch=1u<<8,OwnerTokenMatch=1u<<9,OwnerEpochMatch=1u<<10,OwnerLive=1u<<31};
struct FlexiblePlacementRequest {std::uint32_t version{1};std::int32_t supportX{},supportY{},supportZ{};std::uint32_t targetBlockId{},face{},reserved{};std::int32_t selectedSlot{},itemId{},itemAux{};std::uint32_t itemCount{};float relativeHitX{},relativeHitY{},relativeHitZ{},pitch{},yaw{};};
struct FlexiblePlacementStatusV2 {std::uint32_t version{kVersion2},statusBits{};std::uint64_t ownerToken{},ownerEpoch{},worldGeneration{};std::int32_t dimension{};std::uint32_t reserved{};};
struct FlexiblePlacementRequestV2 {std::uint32_t version{kVersion2},flags{};std::uint64_t ownerToken{},ownerEpoch{},worldGeneration{};std::int32_t dimension{},supportX{},supportY{},supportZ{};std::uint32_t targetBlockId{},face{},reserved{};std::int32_t selectedSlot{},itemId{},itemAux{};std::uint32_t itemCount{};float relativeHitX{},relativeHitY{},relativeHitZ{},pitch{},yaw{},maxPickRange{};std::uint32_t reserved2{};};
struct FlexiblePlacementOwnerQueryV2 {std::uint32_t version{kVersion2},flags{};std::uint64_t ownerToken{},ownerEpoch{},worldGeneration{};std::int32_t dimension{},selectedSlot{},itemId{},itemAux{};std::uint32_t itemCount{},reserved{};};
struct FlexiblePlacementOwnerStatusV2 {std::uint32_t version{kVersion2},flags{};std::uint64_t ownerToken{},ownerEpoch{},worldGeneration{};std::int32_t dimension{};std::uint32_t modeFlags{};std::int32_t selectedSlot{},itemId{},itemAux{};std::uint32_t itemCount{},reserved{};};
}
struct Vec2 { float x{},y{};bool operator==(Vec2 const&) const=default; };
struct Vec3 { float x{},y{},z{};bool operator==(Vec3 const&)const=default; };
class BlockPos { public:int x{},y{},z{};bool operator==(BlockPos const&) const=default;bool operator<(BlockPos const& b) const {return std::tie(x,y,z)<std::tie(b.x,b.y,b.z);} };
template<class T> struct Field {T value{};Field()=default;Field(T v):value(std::move(v)) {} T& get(){return value;} T const& get() const {return value;} Field& operator=(T v){value=std::move(v);return *this;} };
template<class T> struct Field<std::unique_ptr<T>>:std::unique_ptr<T> {using std::unique_ptr<T>::operator=;};
enum class HandSlot {Mainhand,Offhand};
struct Block;
class ItemStack {public:int key{},mCount{},net{};Block* mBlock{};std::string getTypeName()const{return "minecraft:oak_trapdoor";}bool isNull() const{return mCount==0;}int getId()const{return key;}int getIdAux() const{return key;}bool matchesNetIdVariant(ItemStack const& other) const{return net==other.net;} };
struct Block {unsigned mNetworkId{};bool air=true;bool isAir()const{return air;}struct Type{bool mIsOpaqueFullBlock{true};bool isSlabBlock()const{return false;};};Type type{};Type const& getBlockType()const{return type;} };
struct Region {std::map<BlockPos,Block> cells;Block empty{};Block const& getBlock(BlockPos const& p)const {auto it=cells.find(p);return it==cells.end()?empty:it->second;} };
struct Inventory {std::array<ItemStack,9> items{};ItemStack const& getItem(int slot)const{return items.at(slot);} };
class LocalPlayer {public:Inventory inventory;Region region;Vec2 rotation{17,90};Vec3 eye{0,0,0};int selected{};int dimension{};Vec2 const& getRotation()const{return rotation;}int getSelectedItemSlot()const{return selected;}Inventory& getInventory(){return inventory;}Region& getDimensionBlockSource(){return region;}int getDimensionId()const{return dimension;}Vec3 getEyePos()const{return eye;}BlockPos getPosition()const{return {};};float getPickRange()const{return 6.0F;} };
class ComplexInventoryTransaction {public:enum class Type{ItemUseTransaction};Type mType{};virtual ~ComplexInventoryTransaction()=default;static std::unique_ptr<ComplexInventoryTransaction> fromType(Type,struct InventoryTransaction);};
struct InventoryTransaction{};
struct ItemUseInventoryTransaction:ComplexInventoryTransaction {enum class ActionType{Place};enum class TriggerType{PlayerInput};enum class PredictedResult{Success};enum class ClientCooldownState{Off};BlockPos mPos{-1,0,0};unsigned mTargetBlockId=12;int slot{},mSlot{},net=101;uchar face=5,mFace=5;HandSlot mHand{};BlockPos mFromPos{};ActionType mActionType{};TriggerType mTriggerType{};PredictedResult mClientPredictedResult{};ClientCooldownState mClientCooldownState{};struct ItemField{bool mIncludeNetIds{};};Field<ItemField> mItem;Field<Vec3> mClickPos{{1.0F,0.5F,0.5F}};void setSelectedItem(ItemStack const& item){net=item.net;}};
std::unique_ptr<ComplexInventoryTransaction> ComplexInventoryTransaction::fromType(Type,InventoryTransaction){return std::make_unique<ItemUseInventoryTransaction>();}
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
    Field<Vec2> mInteractRotation{{17,23}},mRot{{17,90}};
    float mYHeadRot=91;Field<Vec3> mCameraOrientation{{0,0,1}};
    Field<Vec2> mMove{{.2f,.7f}},mAnalogMoveVector{{.1f,.8f}},mRawMoveVector{{.3f,.6f}};
    struct InputTick{std::uint64_t mValue=1234;};Field<InputTick> mClientTick;
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
bool praxisFeature=true,praxisFocused=true,praxisModifiers=true,praxisWorld=true;
lholo::flexible_placement_abi::FlexiblePlacementOwnerStatusV2 fixturePraxisOwner(
    lholo::flexible_placement_abi::FlexiblePlacementOwnerQueryV2 const& query) {
    using namespace lholo::flexible_placement_abi;
    FlexiblePlacementOwnerStatusV2 result{};
    result.ownerToken=query.ownerToken;result.ownerEpoch=query.ownerEpoch;
    result.worldGeneration=query.worldGeneration;result.dimension=query.dimension;
    result.selectedSlot=query.selectedSlot;result.itemId=query.itemId;
    result.itemAux=query.itemAux;result.itemCount=query.itemCount;
    result.modeFlags=query.flags;
    result.flags=OwnerGameplay|OwnerGuiClear|OwnerDimensionMatch|OwnerStackMatch|OwnerModeMatch
        |OwnerTokenMatch|OwnerEpochMatch;
    if(praxisFeature) result.flags|=OwnerFeatureEnabled;
    if(praxisFocused) result.flags|=OwnerFocused;
    if(praxisModifiers) result.flags|=OwnerModifiers;
    if(praxisWorld) result.flags|=OwnerWorldMatch;
    if(praxisFeature&&praxisFocused&&praxisModifiers&&praxisWorld)
        result.flags|=OwnerLive;
    return result;
}
bool fixtureGetModuleHandleExW(DWORD,LPCWSTR,HMODULE* module){*module=reinterpret_cast<HMODULE>(1);return true;}
FARPROC fixtureGetProcAddress(HMODULE,LPCSTR){return reinterpret_cast<void*>(&fixturePraxisOwner);}
bool fixtureFreeLibrary(HMODULE){return true;}
#define GetModuleHandleExW fixtureGetModuleHandleExW
#define GetProcAddress fixtureGetProcAddress
#define FreeLibrary fixtureFreeLibrary
namespace lholo::place::projection {Block ghost{20,false};bool missing=true;struct Query {Block const* block;bool missing;};Query queryProjection(LocalPlayer&,BlockPos const&){return {&ghost,missing};}
Query queryPlacementProjection(LocalPlayer& p,BlockPos const& pos){return queryProjection(p,pos);}}
namespace lholo::app {
namespace hook_lifecycle {struct DetourGuard {explicit operator bool()const{return true;}};}
template<class F,class E>bool invokeNativeCallback(F&& f,E&& e){try{f();return true;}catch(...){e("supplied fault");return false;}}
void reportNativeCallbackFailure(char const*,char const*){}
}
struct Forward {bool attached{};Vec2 rotation{},main{};float head{},camera{};std::uint64_t tick{};int id{},slots{},net{};};
std::vector<Forward> forwarded;
namespace lholo::place::detail {namespace {
template<class... T> bool rememberLocalPlacementRotation(T&&...){return true;}
template<class... T> void cancelLocalPlacementRotations(T&&...){}
bool installLocalPlacementRotationHook(){return true;}
bool uninstallLocalPlacementRotationHook(){return true;}
#include "RotationDeliveryDecls.inc"
struct RotationAuthInputHook:LoopbackPacketSender {
    static int hook(){return 0;}static bool unhook(){return true;}
    void origin(Packet& packet) {
        Forward f;
        if(packet.getId()==MinecraftPacketIds::PlayerAuthInputPacket) {
            auto& input=static_cast<PlayerAuthInputPacket&>(packet);
            f={input.mItemUseTransaction.get()!=nullptr,input.mInteractRotation.get(),input.mRot.get(),input.mYHeadRot,input.mCameraOrientation.get().x,input.mClientTick.get().mValue};
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
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#include "RotationDeliveryExports.inc"
#pragma clang diagnostic pop
}
int checks{},failures{};
void check(bool v,char const* m){++checks;if(!v){++failures;std::fprintf(stderr,"FAIL %s\n",m);}}
using namespace lholo::place::detail;

int main(){
 using namespace lholo::flexible_placement_abi;
 LocalPlayer player;Block itemBlock{99,false};RotationAuthInputHook hook;auto& state=PlacementState::getInstance();
 auto reset=[&]{uninstallRotationDeliveryHook();state.resetWorldSession();state.setManualPlacementAllowedItems({});player=LocalPlayer{};player.inventory.items[0]={1,64,101,&itemBlock};player.region.cells[{-1,0,0}]={12,false};client={&player,&hook,true};clockMs=1000;gui=false;throwForward=false;forwarded.clear();praxisFeature=praxisFocused=praxisModifiers=praxisWorld=true;installRotationDeliveryHook();};
 auto queue=[&]{FlexiblePlacementRequestV2 r{};r.ownerToken=44;r.ownerEpoch=5;r.worldGeneration=6;r.dimension=0;r.flags=RequestGameplay|RequestFocused|RequestGuiClear|RequestModifiers|RequestRotationSync|RequestRotationMode;r.supportX=-1;r.targetBlockId=12;r.face=5;r.selectedSlot=0;r.itemId=1;r.itemAux=1;r.itemCount=64;r.relativeHitX=1;r.relativeHitY=.5f;r.relativeHitZ=.5f;r.pitch=17;r.yaw=180;r.maxPickRange=6;check(lholo_flexible_placement_v2_queue(r)==QueueAccepted,"actual ABI queue accepts request");};
 auto send=[&](std::uint64_t tick){PlayerAuthInputPacket p;p.mClientTick.get().mValue=tick;auto flags=p.mInputData.get();auto move=p.mMove.get();auto camera=p.mCameraOrientation.get();hook.run(p);check(p.mRot.get()==Vec2{17,90} && p.mInteractRotation.get()==Vec2{17,23} && p.mYHeadRot==91,"production hook restores all original rotations");check(p.mMove.get()==move&&p.mCameraOrientation.get()==camera&&p.mClientTick.get().mValue==tick,"production hook preserves movement camera tick");check(!p.mItemUseTransaction.get()&&p.mInputData.get()==flags,"production hook restores transaction and flags");};
 reset();queue();send(10);check(!forwarded.back().attached&&pending&&pending->warmup.primes==1,"first real packet primes only");send(11);check(!forwarded.back().attached&&pending&&pending->warmup.primes==2,"second real packet primes only");send(12);check(forwarded.back().attached&&!pending,"third packet dispatches exactly once");for(auto const&f:forwarded)check(f.main==Vec2{17,180}&&f.rotation==Vec2{17,180}&&f.head==180,"all forwarded warmup/use rotations coherent");send(13);check(!forwarded.back().attached,"no replay after successful attachment");
 reset();queue();send(10);send(10);check(pending&&pending->warmup.primes==1&&!forwarded.back().attached,"same tick cannot advance warmup");send(9);check(pending&&pending->warmup.primes==1&&!forwarded.back().attached,"backward tick cannot advance warmup");send(11);send(12);check(forwarded.back().attached,"two fresh primes eventually dispatch");
 reset();queue();send(1);praxisFeature=false;send(2);check(!pending&&!forwarded.back().attached,"feature off cancels primed placement");
 reset();queue();send(1);praxisModifiers=false;send(2);check(!pending&&!forwarded.back().attached,"released modifier cancels primed placement");
 reset();queue();send(1);gui=true;send(2);check(!pending&&!forwarded.back().attached,"GUI cancels primed placement");
 reset();queue();send(1);player.selected=1;send(2);check(!pending&&!forwarded.back().attached,"slot change cancels primed placement");
 reset();queue();send(1);clockMs=1500;send(2);check(!pending&&!forwarded.back().attached,"expiry cancels primed placement");
 reset();queue();send(1);throwForward=true;send(2);check(!pending&&!forwarded.back().attached,"ambiguous failed prime is not retried");
 reset();queue();send(1);send(2);throwForward=true;send(3);check(!pending&&forwarded.back().attached,"ambiguous placement forward not replayed");throwForward=false;send(4);check(!forwarded.back().attached,"no duplicate transaction after exception");
 reset();queue();send(1);{PlayerAuthInputPacket p;p.mClientTick.get().mValue=2;p.mInputData.get().insert(PlayerAuthInputPacketPayload::InputData::PerformBlockActions);hook.run(p);}check(pending&&pending->warmup.primes==0&&!forwarded.back().attached,"native action ownership resets priming");send(3);send(4);send(5);check(forwarded.back().attached,"clean input sequence resumes safely after conflict");
 reset();queue();send(1);check(lholo_flexible_placement_v2_cancel(44,5)==QueueAccepted,"cancel removes primed request");send(2);check(!forwarded.back().attached,"canceled priming never places");
 std::printf("ACTUAL_DELIVERY_BODY checks=%d failures=%d; SDK/network boundary doubles, real BDS test separate\n",checks,failures);return failures?1:0;
}
