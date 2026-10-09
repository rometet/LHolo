// Actual PlacementExecutor bodies with explicit offline boundary doubles.
// These doubles supply query/planner/packet results, never native/server success.
#include "place/PlacementState.h"
#include "place/PlacementDirectionRules.h"
#include "place/ManualPlacementRules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using uchar = unsigned char;
using uint = unsigned int;
struct Vec3 { float x{},y{},z{}; };
struct BlockPos {
    int x{},y{},z{};
    bool operator==(BlockPos const&) const = default;
    bool operator<(BlockPos const& p) const { return std::tie(x,y,z)<std::tie(p.x,p.y,p.z); }
    BlockPos operator+(BlockPos const& p) const { return {x+p.x,y+p.y,z+p.z}; }
};
struct Player;
enum class BlockProperty { Door };
struct Facing {
    enum class Name { Down=0,Up=1,North=2,South=3,West=4,East=5 };
    static std::array<BlockPos,6> const& DIRECTION() {
        static std::array<BlockPos,6> const value{{{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}}};return value;
    }
    static std::array<uchar,6> const& OPPOSITE_FACING() {
        static std::array<uchar,6> const value{{1,0,3,2,5,4}};return value;
    }
};
struct ItemStack {
    int key{}, aux{}, count{};
    int netId{};
    bool operator==(ItemStack const&) const = default;
    bool isNull() const { return count==0; }
    int getIdAux() const { return key; }
    int getAuxValue() const { return aux; }
};
struct Block {
    std::string name{"minecraft:stone"};
    std::map<std::string,std::string> states;
    unsigned mNetworkId{1};
    int material{1};
    bool air{}, doubleSlab{}, mismatchAllowed{};
    bool mayPlaceAllowed=true;
    bool interactive{};
    bool operator==(Block const& other) const { return mNetworkId==other.mNetworkId; }
    bool isAir() const { return air; }
    std::string const& getTypeName() const { return name; }
    Block const& getBlockType() const { return *this; }
    bool isSlabBlock() const { return name.ends_with("_slab"); }
    bool allowStateMismatchOnPlacement(Block const&,Block const&) const { return mismatchAllowed; }
    bool hasProperty(BlockProperty) const { return name.ends_with("_door"); }
    bool isInteractiveBlock() const { return interactive; }
    template<class Region> bool mayPlace(Region&,BlockPos const&) const { return mayPlaceAllowed; }
    Block const& getPlacementBlock(Player&,BlockPos const&,uchar,Vec3 const&,int) const;
};
struct BlockSource {
    Block air{.name="minecraft:air",.states={},.mNetworkId=0,.material=0,.air=true};
    std::map<BlockPos,Block> cells;
    Block const& getBlock(BlockPos const& p) const {
        auto it=cells.find(p);return it==cells.end()?air:it->second;
    }
};
struct Inventory { std::array<ItemStack,36> items{}; ItemStack const& getItem(int i) const { return items.at(i); } };
struct Player {
    Inventory inventory; int selected{}; BlockSource region;
    Inventory& getInventory() { return inventory; }
    int getSelectedItemSlot() const { return selected; }
    void setSelectedSlot(int i) { selected=i; }
    Vec3 getPosition() const { return {}; }
    Vec3 getEyePos() const { return {}; }
    Vec3 getViewVector(float) const { return {1,0,0}; }
    float getPickRange() const { return 4; }
    BlockSource& getDimensionBlockSource() { return region; }
};
using LocalPlayer=Player;
struct PredictionInput { BlockPos cell;uchar face;Vec3 relative;int aux; };
std::vector<PredictionInput> predictionInputs;
Block const* suppliedPrediction{};
Block const& Block::getPlacementBlock(Player&,BlockPos const& cell,uchar face,Vec3 const& relative,int aux) const {
    predictionInputs.push_back({cell,face,relative,aux});return suppliedPrediction?*suppliedPrediction:*this;
}
template<class T> struct Field { T value{}; T& get() { return value; } };
enum class ContainerID { Inventory };
enum class InventorySourceType { ContainerInventory };
struct InventorySource {
    enum class InventorySourceFlags { NoFlag };
    InventorySourceType type;ContainerID container;InventorySourceFlags flags;
};
struct InventoryAction {
    InventorySource source;uint slot;ItemStack before,after;
};
struct InventoryTransaction {
    std::vector<InventoryAction> actions;
    void addAction(InventoryAction a) { actions.push_back(std::move(a)); }
};
struct ComplexInventoryTransaction {
    enum class Type { ItemUseTransaction,NormalTransaction };
    Type fixtureType=Type::ItemUseTransaction;
    Field<InventoryTransaction> mTransaction;
    virtual ~ComplexInventoryTransaction()=default;
    static std::unique_ptr<ComplexInventoryTransaction> fromType(Type, InventoryTransaction);
};
enum class HandSlot { Mainhand };
struct ItemUseInventoryTransaction:ComplexInventoryTransaction {
    enum class ActionType { Place }; enum class TriggerType { PlayerInput };
    enum class PredictedResult { Success }; enum class ClientCooldownState { Off };
    struct Descriptor { bool mIncludeNetIds{}; };
    Type mType{}; ActionType mActionType{}; TriggerType mTriggerType{};
    BlockPos mPos{}; uchar mFace{}; int mSlot{}; HandSlot mHand{};
    Vec3 mFromPos{},mClickPos{};
    PredictedResult mClientPredictedResult{}; ClientCooldownState mClientCooldownState{};
    unsigned mTargetBlockId{}; Field<Descriptor> mItem{}; ItemStack selected{};
    void setSelectedItem(ItemStack const& item) { selected=item; }
};
struct InventoryTransactionPacketPayload {
    std::unique_ptr<ComplexInventoryTransaction> tx;
    InventoryTransactionPacketPayload(std::unique_ptr<ComplexInventoryTransaction> p,bool):tx(std::move(p)) {}
};
struct InventoryTransactionPacket {
    InventoryTransactionPacketPayload payload;
    explicit InventoryTransactionPacket(InventoryTransactionPacketPayload p):payload(std::move(p)) {}
};
struct Sent {
    BlockPos pos;Vec3 click;int slot;bool netIds;
    uchar face{};unsigned targetId{};ItemStack item{};std::uint64_t time{};
};
struct SwapSent { std::vector<InventoryAction> actions;std::uint64_t time{}; };
std::uint64_t fixtureTime{};
std::uint64_t GetTickCount64() { return fixtureTime; }
bool factoryAvailable=true, clientAvailable=true, fixtureGui{}, heldExempt{};
bool normalFactoryAvailable=true;
int normalFactoryCalls{};
std::vector<Sent> sent;
std::vector<SwapSent> swapSent;
int swaps{}, planned{};
std::vector<int> plannedCells;
std::map<int,bool> planOutcomes;
bool useActualPlanner{};
struct PacketSender {
    void sendToServer(InventoryTransactionPacket const& packet) {
        if(packet.payload.tx->fixtureType==ComplexInventoryTransaction::Type::NormalTransaction) {
            ++swaps;swapSent.push_back({packet.payload.tx->mTransaction.value.actions,fixtureTime});return;
        }
        auto const& tx=static_cast<ItemUseInventoryTransaction const&>(*packet.payload.tx);
        sent.push_back({tx.mPos,tx.mClickPos,tx.mSlot,tx.mItem.value.mIncludeNetIds,tx.mFace,tx.mTargetBlockId,tx.selected,fixtureTime});
    }
};
struct Client {
    Player* player{}; bool inputEnabled=true; PacketSender sender;
    Player* getLocalPlayer() const { return player; }
    bool isInGameInputEnabled() const { return inputEnabled; }
    PacketSender& getPacketSender() { return sender; }
} fixtureClient;
namespace ll::service { Client* getClientInstance() { return clientAvailable?&fixtureClient:nullptr; } }
std::unique_ptr<ComplexInventoryTransaction> ComplexInventoryTransaction::fromType(Type type,InventoryTransaction) {
    if(type==Type::NormalTransaction) {
        ++normalFactoryCalls;
        if(!normalFactoryAvailable) return nullptr;
        auto tx=std::make_unique<ComplexInventoryTransaction>();tx->fixtureType=type;return tx;
    }
    return factoryAvailable?std::make_unique<ItemUseInventoryTransaction>():nullptr;
}
namespace lholo::place {
namespace block {
ItemStack makePlacementItem(Block const& b) { return {b.material,0,1}; }
ItemStack makeManualPlacementItem(Block const& b) { return makePlacementItem(b); }
#include "BlockIdentity.inc"
}
namespace detail {
struct PlacementContext { Vec3 eye; float reachSquared; std::int64_t eyeX{},eyeY{},eyeZ{}; int viewX{},viewY{},viewZ{}; };
}
namespace projection {
struct Candidate { int x{},y{},z{};Block const* block{};bool missing=true; };
std::vector<Candidate> candidates;
std::vector<Candidate> queryMissingCellsInRange(Player&,Vec3 const&,float) { return candidates; }
Candidate queryProjection(Player&,BlockPos const& p) {
    for(auto const& c:candidates) if(c.x==p.x&&c.y==p.y&&c.z==p.z) return c;
    return {};
}
namespace detail {
bool checkedVoxelRayOrigin(std::array<float,3>,std::array<float,3>,float) { return true; }
}
}
namespace structure {
bool isGuiVisible() { return fixtureGui; }
bool shouldShowProjectedBlockName() { return false; }
}
namespace i18n {
enum class TextKey { ActionHintNoMatchingItem,ActionHintManualModeBlocked };
struct Message { TextKey key; };
}
namespace app {
namespace hook_lifecycle { struct DetourGuard { explicit operator bool() const { return true; } }; }
template<class F,class E> bool invokeNativeCallback(F&& fn,E&&) { fn();return true; }
void reportNativeCallbackFailure(char const*,char const*) {}
}
namespace structure {
std::vector<i18n::TextKey> hints;
void showActionHint(i18n::Message const& m) { hints.push_back(m.key); }
}
namespace detail {
enum class ManualTargetStatus { None,Ready,MissingMaterial };
ManualTargetStatus fixtureTargetStatus=ManualTargetStatus::None;
ManualTargetStatus manualTargetStatusUnderCrosshair(Player&) { return fixtureTargetStatus; }
}
namespace {
using detail::FailedPlanKey;using detail::FailedPlanKeyHash;using detail::PlacementContext;
auto& placementState() { return detail::PlacementState::getInstance(); }
#include "ConstantsBody.inc"
struct ProjectionTarget { BlockPos cell,at;uchar face{};Block const* block{};Vec3 clickPos{}; };
std::optional<ProjectionTarget> aimed;
bool validFace(uchar f) { return f<6; }
BlockPos neighborOf(BlockPos const& p,uchar f) { return p+Facing::DIRECTION().at(f); }
uchar oppositeFace(uchar f) { return Facing::OPPOSITE_FACING().at(f); }
uchar faceToward(Vec3 const& v) {
    float x=std::abs(v.x),y=std::abs(v.y),z=std::abs(v.z);
    if(x>=y&&x>=z) return v.x>0?5:4;
    if(y>=z) return v.y>0?1:0;
    return v.z>0?3:2;
}
std::string serializedState(Block const& b,char const* k) {
    auto it=b.states.find(k);return it==b.states.end()?"":it->second;
}
bool sameSerializedState(Block const& p,Block const& g,char const* k) {
    auto v=serializedState(g,k);return !v.empty()&&serializedState(p,k)==v;
}
bool sameFirstPresentSerializedState(Block const& p,Block const& g,char const* a,char const* b) {
    return sameSerializedState(p,g,serializedState(g,a).empty()?b:a);
}
bool placementDirectionMatches(Block const& p,Block const& g) {
    return detail::placementDirectionStatesMatch(detail::placementDirectionRule(g.name),
        [&](char const* k){return serializedState(p,k);},[&](char const* k){return serializedState(g,k);});
}
bool isTwoBlockDoor(Block const& b) { return b.name.ends_with("_door")&&!serializedState(b,"upper_block_bit").empty(); }
bool isDoubleSlab(Block const& b) { return b.doubleSlab; }
bool manualSerializedPlacementMatches(Block const& p,Block const& g) {
    return detail::manualPlacementStateMapsMatch(g.name,g.states,p.states);
}
bool isFastOpaquePlacementCandidate(Block const& b) { return b.name=="minecraft:stone"; }
bool isViewSensitivePlan(Block const& b) { return !isFastOpaquePlacementCandidate(b); }
std::optional<uchar> deterministicSupportDirection(Block const& b) {
    auto facing=serializedState(b,"facing_direction");
    if(facing.empty()) facing=serializedState(b,"minecraft:facing_direction");
    return detail::deterministicSupportFace(b.name,facing,serializedState(b,"torch_facing_direction"));
}
bool wouldMergeClickedSlab(Block const& a,Block const& b) {
    return a.isSlabBlock()&&b.isSlabBlock()&&!a.doubleSlab&&!b.doubleSlab&&a.material==b.material;
}
bool placementPredictionMatches(Block const&,Block const&,Block const*);
// Rename only the entry symbol; the extracted production planner body and all
// its decisions stay unchanged. Scheduling tests above supply planner outcomes;
// routing tests below call this actual body with a controlled predictor result.
#define resolveOrientedPlacement resolveOrientedPlacementActual
#include "PlannerBody.inc"
#undef resolveOrientedPlacement
#include "SwapBody.inc"
bool resolveOrientedPlacement(Player& player,BlockSource& region,PlacementContext const& context,BlockPos const& cell,
    Block const& b,int aux,ProjectionTarget& target) {
    ++planned;plannedCells.push_back(cell.x);
    if(useActualPlanner) return resolveOrientedPlacementActual(player,region,context,cell,b,aux,target);
    if(!planOutcomes[cell.x]) return false;
    target={cell,cell,1,&b,{static_cast<float>(cell.x)+0.5f,0.25f,0.5f}};
    return true;
}
PlacementContext makePlacementContext(Vec3 const& eye,Vec3 const&,float range) { return {eye,range*range}; }
void consumeBrokenProjectionCells(Player&,std::uint64_t) {}
void updateAimedProjectedBlockName(Block const*) {}
std::optional<ProjectionTarget> findProjectionTarget(Player&,Vec3 const&,Vec3 const&,float) { return aimed; }
bool isManualPlacementHeldItemAllowed(Player&) { return heldExempt; }
#include "ExecutorBodies.inc"
struct FixtureGameMode;
bool isLocalManualBuild(FixtureGameMode&);
bool aimedBlockAcceptsRightClick(FixtureGameMode&,BlockPos const&);
void cancelPendingManualPress() { placementState().cancelManualPress(); }
struct FixtureGameMode {
    Player& mPlayer;bool local=true;int origins{};
    void origin(BlockPos const&,uchar,HandSlot) { ++origins; }
    void run(BlockPos const& pos,uchar face,HandSlot handSlot) {
#include "StartBuildBody.inc"
    }
};
bool isLocalManualBuild(FixtureGameMode& gm) { return gm.local&&placementState().manualMode(); }
bool aimedBlockAcceptsRightClick(FixtureGameMode& gm,BlockPos const& pos) {
    return gm.mPlayer.getDimensionBlockSource().getBlock(pos).getBlockType().isInteractiveBlock();
}
} // namespace
} // namespace lholo::place

int checks{},failures{};
void check(bool ok,char const* message) {
    ++checks;if(!ok) { ++failures;std::fprintf(stderr,"FAIL: %s\n",message); }
}
using namespace lholo::place;
void reset(Player& p) {
    placementState().resetWorldSession();p=Player{};fixtureClient.player=&p;fixtureClient.inputEnabled=true;
    fixtureTime=1000;factoryAvailable=true;clientAvailable=true;fixtureGui=false;heldExempt=false;
    sent.clear();swaps=0;planned=0;plannedCells.clear();planOutcomes.clear();projection::candidates.clear();aimed.reset();
    predictionInputs.clear();suppliedPrediction=nullptr;
    useActualPlanner=false;
    normalFactoryAvailable=true;normalFactoryCalls=0;swapSent.clear();structure::hints.clear();detail::fixtureTargetStatus=detail::ManualTargetStatus::None;
}
#include "DeliveryChecks.h"
#include "CoverageChecks.h"
int main() {
    Player player;Block stone;reset(player);
    // Native predictor boundary inputs, all support faces and +/-section edges.
    // A matching predictor output is SUPPLIED, not claimed to be engine-proven.
    for(auto cell:{BlockPos{-17,64,16},BlockPos{16,-4,-17}}) for(uchar sf=0;sf<6;++sf) {
        reset(player);Block trapdoor{.name="minecraft:oak_trapdoor",.states={{"direction","2"},{"upside_down_bit","1"},{"open_bit","0"}},.mNetworkId=2};
        Block predicted=trapdoor;suppliedPrediction=&predicted;
        auto support=neighborOf(cell,sf);player.region.cells[support]=stone;
        PlacementContext context{{static_cast<float>(cell.x)+0.5f,static_cast<float>(cell.y)+0.5f,static_cast<float>(cell.z)+0.5f},64};
        ProjectionTarget target;
        check(resolveOrientedPlacementActual(player,player.region,context,cell,trapdoor,7,target),"actual planner admits supplied matching support prediction");
        check(target.at==support&&target.face==oppositeFace(sf),"actual planner chooses clicked support and opposite face");
        check(!predictionInputs.empty()&&predictionInputs.front().cell==cell&&predictionInputs.front().aux==7,"native prediction receives target cell and actual stack aux");
        auto const& input=predictionInputs.front();
        check(input.relative.x>=0&&input.relative.x<=1&&input.relative.y>=0&&input.relative.y<=1&&input.relative.z>=0&&input.relative.z<=1,"predictor receives support-relative hit across world heights");
        check(placeBlock(player,target,0,{1,7,64}),"actual transaction sends planned target");
        check(sent.back().pos==support&&sent.back().click.x==input.relative.x&&sent.back().click.y==input.relative.y&&sent.back().click.z==input.relative.z,"transaction click agrees with predictor relative hit");
        std::printf("planner cell=(%d,%d,%d) support=(%d,%d,%d) face=%u aux=%d hit=(%.2f,%.2f,%.2f) predicted=supplied-match send=called apply=NOT_RUN\n",
            cell.x,cell.y,cell.z,support.x,support.y,support.z,static_cast<unsigned>(input.face),input.aux,
            input.relative.x,input.relative.y,input.relative.z);
        player.region.cells[cell]=stone;
        check(!resolveOrientedPlacementActual(player,player.region,context,cell,trapdoor,7,target),"stale occupied target rejected before predictor");
    }
    // Existing deterministic hopper/torch route intentionally bypasses the
    // predictor. Record that boundary rather than treating it as native success.
    for(auto facing:{"0","2","3","4","5"}) {
        reset(player);Block hopper{.name="minecraft:hopper",.states={{"facing_direction",facing}},.mNetworkId=2};
        auto sf=*deterministicSupportDirection(hopper);player.region.cells[neighborOf({0,0,0},sf)]=stone;
        ProjectionTarget target;
        check(resolveOrientedPlacementActual(player,player.region,makePlacementContext({}, {}, 4),{},hopper,0,target),"deterministic hopper support route retained");
        check(predictionInputs.empty(),"hopper deterministic shortcut is explicitly not native-prediction evidence");
    }
    reset(player);
    // Same material in hotbar and backpack. Real snapshot insertion + lookup.
    player.inventory.items[2]={1,0,64};player.inventory.items[19]={1,0,64};
    auto found=findItemSlot(snapshotInventory(player),stone);
    std::printf("inventory duplicate material selected slot=%d\n",found.slot);
    check(found.slot==2,"range lookup preserves hotbar preference");
    projection::candidates={{0,0,0,&stone}};planOutcomes[0]=true;
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==1&&swaps==0,"range sends available hotbar item without a swap");

    // Permanently impossible prefix exceeds 16 * (250/50) candidates.
    reset(player);Block oriented{.name="minecraft:piston",.states={},.mNetworkId=2};
    player.inventory.items[0]={1,0,64};
    for(int i=0;i<100;++i) projection::candidates.push_back({i,0,0,&oriented});
    planOutcomes[95]=true;
    for(int i=0;i<200&&sent.empty();++i) { fixtureTime=1000+50*i;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4)); }
    auto highest=plannedCells.empty()?-1:*std::max_element(plannedCells.begin(),plannedCells.end());
    std::printf("range impossible-prefix highest planned=%d, send calls=%zu, plans=%d, last tick=%llu\n",highest,sent.size(),planned,static_cast<unsigned long long>(fixtureTime));
    check(sent.size()==1,"range eventually reaches valid tail beyond failed-plan cache cycle");

    // A separate fast budget must not drag the complex resume point backwards.
    reset(player);player.inventory.items[0]={1,0,64};
    for(int i=0;i<100;++i) projection::candidates.push_back({i,0,0,&oriented});
    for(int i=100;i<170;++i) projection::candidates.push_back({i,0,0,&stone});
    planOutcomes[95]=true;bool budgetPreserved=true;
    for(int i=0;i<200&&sent.empty();++i) {
        auto prior=plannedCells.size();fixtureTime=1000+50*i;
        tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
        int complex=0,fast=0;
        for(auto p=prior;p<plannedCells.size();++p) { if(plannedCells[p]<100) ++complex;else ++fast; }
        budgetPreserved=budgetPreserved&&complex<=16&&fast<=64;
    }
    check(sent.size()==1,"mixed fast/complex plans eventually reach valid complex tail");
    check(budgetPreserved,"range retains 16 complex and 64 fast per tick budgets");
    // Empty/shrunk lists retire safely; the persistent cursor is modulo current size.
    projection::candidates.clear();tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    projection::candidates={{7,0,0,&stone}};planOutcomes[7]=true;fixtureTime+=50;
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==2,"range tolerates empty and shrunk candidate lists");

    // One native callback per logical press; repeated callbacks cannot requeue.
    reset(player);auto& state=placementState();state.setManualMode(true);
    check(state.beginManualPress(1000,state.manualInputEpoch()),"first manual press accepted");
    state.setManualPlaceRequested(false);
    check(!state.beginManualPress(1050,state.manualInputEpoch())&&!state.manualPlaceRequested(),"held callback stays single press");
    auto oldEpoch=state.manualInputEpoch();state.setRangeEnabled(true);
    check(!state.manualHeld()&&!state.manualPlaceRequested(),"range transition clears manual input");
    check(state.manualInputEpoch()!=oldEpoch,"range transition advances input epoch");
    check(!state.beginManualPress(1100,oldEpoch),"range transition rejects late native press");
    reset(player);state.setManualMode(true);check(state.beginManualPress(1000,state.manualInputEpoch()),"manual press before menu mode apply");
    auto mode=state.modes();mode.range=true;check(state.applyModes(mode),"fresh mode choice applied");
    check(!state.manualHeld()&&!state.manualPlaceRequested(),"atomic mode apply clears unchanged manual flag input");
    reset(player);state.setManualMode(true);check(state.beginManualPress(1000,state.manualInputEpoch()),"press before easy mode transition");
    oldEpoch=state.manualInputEpoch();state.setEnabled(true);
    check(!state.manualHeld()&&!state.manualPlaceRequested()&&state.manualInputEpoch()!=oldEpoch,"easy transition cancels old press publication");

    // GUI/pause prevents an old held action from resuming after input returns.
    reset(player);state.setManualMode(true);check(state.beginManualPress(1000,state.manualInputEpoch()),"press before input suspension");
    fixtureClient.inputEnabled=false;tickEasyPlaceImpl(player);
    check(!state.manualHeld()&&!state.manualPlaceRequested(),"game input suspension cancels queued and held manual action");
    reset(player);state.setManualMode(true);check(state.beginManualPress(1000,state.manualInputEpoch()),"press before LHolo GUI");
    fixtureGui=true;tickEasyPlaceImpl(player);
    check(!state.manualHeld()&&!state.manualPlaceRequested(),"LHolo GUI cancels manual action");

    // Quick tap survives release and sends once. Held repeats preserve policy.
    reset(player);state.setManualMode(true);player.inventory.items[0]={1,0,64};planOutcomes[0]=true;
    aimed=ProjectionTarget{{0,0,0},{0,0,0},1,&stone,{0.5f,0.25f,0.5f}};
    check(state.beginManualPress(1000,state.manualInputEpoch()),"quick tap admitted");state.releaseManualPress();
    tickEasyPlaceImpl(player);fixtureTime=1050;tickEasyPlaceImpl(player);
    check(sent.size()==1&&!state.manualPlaceRequested(),"quick tap sends once after release");

    // Real send guard + unchanged 500ms window, next tick inventory boundary.
    reset(player);player.inventory.items[19]={1,0,64};projection::candidates={{0,0,0,&stone}};planOutcomes[0]=true;
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(swaps==1&&sent.empty(),"backpack swap never places in same tick");
    fixtureTime=1050;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(swaps==1&&sent.empty(),"unreflected swap respects 200ms retry floor");
    player.inventory.items[0]=player.inventory.items[19];player.inventory.items[19]={};
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==1,"reflected inventory can send on later tick");
    fixtureTime=1549;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==1,"same cell remains locked at 499ms");
    fixtureTime=1550;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==2,"unapplied cell retries at 500ms; send is not acknowledgement");
    player.region.cells[{0,0,0}]=stone;fixtureTime=2200;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==2,"observed occupied cell cannot send again");
    check(sent[0].netIds&&sent[0].click.x==0.5f&&sent[0].click.y==0.25f,"wire descriptor includes net id and relative click");

    // Each supplied state transition is comparison-only, never a native result.
    for(auto name:{"minecraft:hopper","minecraft:piston","minecraft:sticky_piston","minecraft:observer","minecraft:dispenser","minecraft:dropper"}) {
        for(int f=0;f<6;++f) {
            if(std::string_view(name)=="minecraft:hopper"&&f==1) continue;
            Block ghost{.name=name,.states={{"facing_direction",std::to_string(f)},{"powered_bit","1"}},.mNetworkId=2};
            Block predicted=ghost;predicted.mNetworkId=3;predicted.states["powered_bit"]="0";
            check(placementPredictionMatches(predicted,ghost),"directional family permits post-placement signal difference");
            predicted.states["facing_direction"]=std::to_string((f+1)%6);
            check(!placementPredictionMatches(predicted,ghost),"directional family rejects wrong facing");
        }
    }
    for(auto name:{"minecraft:unpowered_repeater","minecraft:unpowered_comparator"}) {
        for(int d=0;d<4;++d) {
            Block ghost{.name=name,.states={{"direction",std::to_string(d)},{"repeater_delay","3"},{"output_subtract_bit","1"}},.mNetworkId=2};
            Block predicted=ghost;predicted.mNetworkId=3;predicted.states["repeater_delay"]="0";predicted.states["output_subtract_bit"]="0";
            check(placementPredictionMatches(predicted,ghost),"redstone direction permits post-use delay and mode");
            predicted.states["direction"]=std::to_string((d+1)%4);
            check(!placementPredictionMatches(predicted,ghost),"redstone direction remains strict");
        }
    }
    for(int d=0;d<4;++d) for(int half=0;half<2;++half) {
        for(auto name:{"minecraft:oak_stairs","minecraft:oak_trapdoor"}) {
            auto key=std::string_view(name).ends_with("_stairs")?"weirdo_direction":"direction";
            Block ghost{.name=name,.states={{key,std::to_string(d)},{"upside_down_bit",std::to_string(half)},{"open_bit","1"}},.mNetworkId=2};
            Block predicted=ghost;predicted.mNetworkId=3;predicted.states["open_bit"]="0";
            check(placementPredictionMatches(predicted,ghost),"stairs/trapdoor direction and half match");
            predicted.states["upside_down_bit"]=std::to_string(1-half);
            check(!placementPredictionMatches(predicted,ghost),"stairs/trapdoor reject wrong half");
        }
    }
    for(auto half:{"bottom","top"}) {
        Block ghost{.name="minecraft:stone_slab",.states={{"minecraft:vertical_half",half}},.mNetworkId=2};
        Block predicted=ghost;predicted.mNetworkId=3;
        check(placementPredictionMatches(predicted,ghost),"slab correct half accepted");
        predicted.states["minecraft:vertical_half"]=std::string_view(half)=="top"?"bottom":"top";
        check(!placementPredictionMatches(predicted,ghost),"slab wrong half rejected");
    }
    for(auto face:{"top","north","south","east","west"}) {
        Block ghost{.name="minecraft:redstone_torch",.states={{"torch_facing_direction",face}},.mNetworkId=2};
        Block predicted=ghost;predicted.mNetworkId=3;
        check(placementPredictionMatches(predicted,ghost),"torch matching support state accepted");
        predicted.states["torch_facing_direction"]="unknown";
        check(!placementPredictionMatches(predicted,ghost),"torch wrong support state rejected");
    }
    reset(player);
    for(int d=0;d<4;++d) for(int hinge=0;hinge<2;++hinge) {
        Block ghost{.name="minecraft:oak_door",.states={{"direction",std::to_string(d)},{"upper_block_bit","0"},{"open_bit","1"}},.mNetworkId=2};
        Block upper=ghost;upper.states["upper_block_bit"]="1";upper.states["door_hinge_bit"]=std::to_string(hinge);
        Block predicted=ghost;predicted.mNetworkId=3;predicted.states["open_bit"]="0";predicted.states["door_hinge_bit"]=std::to_string(hinge);
        check(placementPredictionMatches(predicted,ghost,&upper),"door post-use open state does not block easy/range plan");
        predicted.states["door_hinge_bit"]=std::to_string(1-hinge);
        check(!placementPredictionMatches(predicted,ghost,&upper),"door hinge remains strict");
    }
    runDeliveryChecks(player);
    runCoverageChecks(player);
    std::printf("ExecutorFixture: %d checks, %d failures; native/BDS/apply/consumption NOT_RUN\n",checks,failures);
    return failures?1:0;
}
