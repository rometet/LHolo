#include "structure/SchematicRuntime.h"
#include "structure/InventoryContents.h"
#include "structure/PlacementMigration.h"
#include "structure/TransientPlacementCache.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"
#include "structure/StructurePaths.h"
#include "structure/capture/StructureCapture.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionCoordinateBounds.h"
#include "projection/Projection.h"
#include "block/BlockPlacementRules.h"
#include "plugin/LHolo.h"
#include "io/AtomicOutput.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/Direction.h"
#include "mc/world/level/block/Block.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include <algorithm>
#include <array>
#include <mutex>
#include <tuple>
#include <Windows.h>

namespace lholo::structure::schematic {
namespace {
std::mutex gMutex;
PlacementSession gSession;
TransientPlacementCache gTransientDimensions;
std::vector<std::string> gFiles;
std::string gWorldKey, gStatus;
int gDimension{};
Cell gFeet;
bool gAvailable{}, gActivate{}, gApply{};
ManualVerificationControl gVerification;
std::uint64_t gWorldEpoch{};
std::uint64_t gActiveId{};
std::shared_ptr<Report const> gReport;
MistakeSelection gSelection;
std::uint64_t gReportRevision{};
std::uint64_t gFilterRevision{};
std::filesystem::path gActivePath;

bool contextValid() { auto const view=capture::getClientViewSnapshot();return gAvailable && view && view->worldEpoch==gWorldEpoch; }
std::filesystem::path library() { return LHolo::getInstance().getSelf().getModDir() / "schematics"; }
void retireReport() { gReport.reset();gSelection.clear(); }
void invalidateVerification() { gVerification.invalidate();retireReport(); }
ReportStamp stampFor(SavedPlacement const& p,std::uint64_t revision,std::uint64_t generation,
    std::uint64_t reportRevision) {
    return {gWorldEpoch,gDimension,revision,p.id,generation,reportRevision,p.origin,p.rotation,p.mirror,
        p.layerAxis,p.layerMode,p.layer,p.visible,p.countExtras,gFilterRevision};
}
bool setFilter(MistakeFilter filter) {
    auto const previous=gSelection.filter();
    if(!gSelection.setFilter(filter))return false;
    if(previous!=filter){
        if(++gFilterRevision==0)++gFilterRevision;
        if(gReport){auto next=std::make_shared<Report>(*gReport);next->stamp.filterRevision=gFilterRevision;gReport=std::move(next);}
    }
    return true;
}
bool placementCurrent(ReportStamp const& stamp,detail::StructureSessionSnapshot const& structure) {
    if(!structure.loaded || !structure.saved.available || structure.loaded->generation!=stamp.loadedGeneration)return false;
    auto const& t=structure.transform;
    Cell const origin{static_cast<std::int64_t>(structure.saved.anchorX)+t.offsetX,
        static_cast<std::int64_t>(structure.saved.anchorY)+t.offsetY,
        static_cast<std::int64_t>(structure.saved.anchorZ)+t.offsetZ};
    return stamp.placementOrigin==origin && stamp.placementRotation==t.rotation && stamp.placementMirror==t.mirror
        && stamp.layerAxis==t.layerAxis && stamp.layerMode==t.layerDisplayMode && stamp.layer==t.displayLayer
        && stamp.visible==t.visible && stamp.countExtras==t.countExtras;
}
bool reportCurrent(detail::StructureSessionSnapshot const& structure) {
    auto const& loaded=structure.loaded;
    if(!contextValid() || !loaded || !gReport || !gReport->stamp.valid() || loaded->sourcePath!=gActivePath)return false;
    auto const session=gSession.snapshot();auto const p=selectedPlacement(session.document);
    return p && p->id==gActiveId && p->dimension==gDimension
        && gReport->stamp.sameContext(stampFor(*p,session.revision,loaded->generation,gReport->stamp.reportRevision))
        && placementCurrent(gReport->stamp,structure);
}

bool change(PlacementSessionSnapshot const& snap, PlacementDocument document) {
    if (!gSession.replace(std::move(document),snap.revision)) return false;
    invalidateVerification(); return true;
}
struct Job {
    std::shared_ptr<LoadedStructure const> loaded;
    SavedPlacement placement;
    std::uint64_t revision{};
    std::uint64_t requestSerial{};
    PlacementTransform transform;
    Rotation rotation{Rotation::None};
    Mirror mirror{Mirror::None};
    Cell observer;
    std::size_t block{};
    RegionScanCursor air;
    std::vector<double> regionWork;
    double totalWork{};
    Report result;
    std::array<std::vector<Mismatch>,3> nearest;
    std::map<std::string,MaterialCount> materials;
    std::map<Block const*,std::pair<std::string,unsigned>> itemCache;
    std::map<std::string,int> inventory;
    std::map<std::string,MaterialCount>::const_iterator materialCursor;
    bool finalizing{};
};
// Accessed exclusively by the existing game tick. Reset requests cross via
// values/revision; reset() never touches a native job on the Present thread.
std::optional<Job> gJob;
bool visible(Job const& job, Cell local, int material = -1, int liquidMaterial = -1) {
    return projection::detail::isLayerVisible(layerOf(job.transform,local,job.placement.layerAxis),
        job.placement.layerMode,job.placement.layer,material,liquidMaterial,job.placement.layerAxis);
}
bool ready(BlockSource& source, BlockPos const& pos, Block const& actual) {
    auto const name = actual.getTypeName();
    return source.areChunksFullyLoaded(pos,0) && !placeholderBlock(name);
}
std::string describe(Block const* value) {
    if (!value) return "minecraft:air";
    return value->mSerializationId.get().toString();
}
void retainMismatch(Job& job, Mismatch row) {
    constexpr std::size_t cap = 512;
    auto const group = row.kind == VerificationState::Missing ? 0u : row.kind == VerificationState::WrongState ? 1u : 2u;
    if(retainNearest(job.nearest[group],std::move(row),cap))job.result.truncated=true;
}
void record(Job& job, Cell world, VerificationState state, Block const* expected, Block const* actual,
    bool expects,bool actualLiquid=false,bool actualExtra=false) {
    job.result.tally.add(state,expects);
    ++job.result.checked;
    if (matchesFilter(state,MistakeFilter::Mistakes)) {
        auto const dx = static_cast<double>(world.x - job.observer.x), dy = static_cast<double>(world.y - job.observer.y), dz = static_cast<double>(world.z - job.observer.z);
        retainMismatch(job,{state,world,describe(expected),describe(actual),dx*dx+dy*dy+dz*dz,actualLiquid,actualExtra});
    }
}
LoadedStructure::RenderBlock const* entryAt(Job const& job,Cell local) {
    auto const it=std::lower_bound(job.loaded->renderBlocks.begin(),job.loaded->renderBlocks.end(),local,
        [](auto const& entry,Cell p){return std::tuple{entry.x,entry.y,entry.z}<std::tuple{p.x,p.y,p.z};});
    return it!=job.loaded->renderBlocks.end() && Cell{it->x,it->y,it->z}==local?&*it:nullptr;
}
void addMaterial(Job& job, Block const* expected, VerificationState state,Cell local,BlockSource& source) {
    if (!expected) return;
    auto const door=expected->getState<bool>(VanillaStates::UpperBlockBit());
    auto const head=expected->getState<bool>(VanillaStates::HeadPieceBit());
    unsigned quantity=requiredItemRule(expected->getTypeName()).quantity;
    if(door || head){
        Cell mate=local;bool const other=* (door?door:head);
        if(door)mate.y+=other?-1:1;
        else {
            auto const direction=expected->getState<int>(VanillaStates::Direction());
            if(direction && *direction>=0 && *direction<4){
                // Native direction vectors; placement/block-state rotation is
                // still delegated to the game's transformer.
                mate.x+=Direction::STEP_X()[*direction]*(other?-1:1);
                mate.z+=Direction::STEP_Z()[*direction]*(other?-1:1);
            }
        }
        auto const peer=mate==local?nullptr:entryAt(job,mate);
        bool const peerVisible=peer && peer->block && peer->block->getTypeName()==expected->getTypeName()
            && visible(job,mate,peer->materialIndex,peer->liquidMaterialIndex);
        quantity=requiredItemRule(expected->getTypeName(),door.value_or(false),head.value_or(false),peerVisible).quantity;
        if(peerVisible && quantity && state==VerificationState::Correct){
            auto const world=job.transform.toWorld(mate);
            BlockPos const pos{static_cast<int>(world.x),static_cast<int>(world.y),static_cast<int>(world.z)};
            auto const& actual=source.getBlock(pos);
            auto const* required=projection::detail::transformExpectedBlock(peer->block,job.rotation,job.mirror,job.placement.rotation==0 && job.placement.mirror==0);
            if(!ready(source,pos,actual) || !required || placeholderBlock(required->getTypeName()))state=VerificationState::Unknown;
            else if(!projection::detail::projectionStatesMatch(*required,actual))state=VerificationState::WrongState;
        }
    }
    if(!quantity)return;
    auto it = job.itemCache.find(expected);
    if (it == job.itemCache.end()) {
        auto const rule = requiredItemRule(expected->getTypeName());
        auto item = block::resolvePlacementItem(*expected);
        auto const key = rule.alias.empty() ? item.itemId : std::string{rule.alias};
        it = job.itemCache.emplace(expected,std::pair{key,rule.quantity}).first;
    }
    auto const key = it->second.first.empty() ? "unmapped: " + std::string{expected->getTypeName()} : it->second.first;
    job.materials[key].add(quantity,state);
}
void scanBlock(Job& job, LocalPlayer& player) {
    auto const& entry = job.loaded->renderBlocks[job.block++];
    Cell const local{entry.x,entry.y,entry.z};
    if (!visible(job,local,entry.materialIndex,entry.liquidMaterialIndex)) return;
    auto const world = job.transform.toWorld(local);
    BlockPos const pos{static_cast<int>(world.x),static_cast<int>(world.y),static_cast<int>(world.z)};
    auto& source = player.getDimensionBlockSource();
    bool const identity = job.placement.rotation == 0 && job.placement.mirror == 0;
    auto const* expected = projection::detail::transformExpectedBlock(entry.block,job.rotation,job.mirror,identity);
    auto const* liquid = projection::detail::transformExpectedBlock(entry.liquid,job.rotation,job.mirror,identity);
    auto const& actual = source.getBlock(pos);
    auto const bubble=(expected && expected->getTypeName()=="minecraft:bubble_column") || actual.getTypeName()=="minecraft:bubble_column";
    auto const& actualLiquid = bubble ? source.getExtraBlock(pos) : source.getLiquidBlock(pos);
    auto const match = [&](Block const* e, Block const& a) {
        if(!e || placeholderBlock(e->getTypeName()) || !ready(source,pos,a))return VerificationState::Unknown;
        return classifyCell(true,true,false,a.isAir(),
            !e || block::placeableBaseName(e->getTypeName()) == block::placeableBaseName(a.getTypeName()),
            !e || projection::detail::projectionStatesMatch(projection::detail::withFlattenedConnections(*e,source,pos),a),job.placement.countExtras);
    };
    auto const bodyState=expected?match(expected,actual):VerificationState::Ignored;
    auto const liquidState=liquid?match(liquid,actualLiquid):VerificationState::Ignored;
    auto state=combineExpectedStates(bodyState,liquidState);
    bool solidInLiquidCell{};
    if(!expected && liquid && state!=VerificationState::Unknown && state!=VerificationState::Missing
        && !actual.isAir() && actual.getTypeName()!=liquid->getTypeName()){
        state=ready(source,pos,actual)?VerificationState::WrongType:VerificationState::Unknown;
        solidInLiquidCell=true;
    }
    bool const rowLiquid=!solidInLiquidCell && (!expected || (liquid && liquidState==state && bodyState!=state));
    auto const* rowExpected=rowLiquid?liquid:(expected?expected:liquid);
    auto const* rowActual=rowLiquid?&actualLiquid:&actual;
    record(job,world,state,rowExpected,rowActual,true,rowLiquid,rowLiquid && bubble);
    addMaterial(job,entry.block,bodyState,local,source); addMaterial(job,entry.liquid,liquidState,local,source);

}
void scanAir(Job& job, LocalPlayer& player,std::size_t& budget) {
    auto const cell=job.air.next(job.loaded->regions,budget);
    if(!cell)return;
    auto const local=*cell;
    if(!visible(job,local))return;
    auto const found = std::lower_bound(job.loaded->renderBlocks.begin(),job.loaded->renderBlocks.end(),local,
        [](auto const& e, Cell p){return std::tuple{e.x,e.y,e.z} < std::tuple{p.x,p.y,p.z};});
    if (found != job.loaded->renderBlocks.end() && Cell{found->x,found->y,found->z} == local) return;
    auto const world = job.transform.toWorld(local);
    BlockPos const pos{static_cast<int>(world.x),static_cast<int>(world.y),static_cast<int>(world.z)};
    auto& source = player.getDimensionBlockSource(); auto const& actual = source.getBlock(pos);
    auto const state = classifyCell(true,ready(source,pos,actual),true,actual.isAir(),false,false,job.placement.countExtras);
    record(job,world,state,nullptr,&actual,false);
}
}
Snapshot snapshot() {
    // StructureSession is sampled before the schematic mutex, never beneath
    // it. The render getter below avoids this session lock entirely.
    auto const structure=detail::StructureSession::getInstance().snapshot();
    std::lock_guard lock(gMutex);
    if(gReport && !reportCurrent(structure))invalidateVerification();
    Snapshot s{gSession.snapshot(),gFiles,gReport,{},contextValid(),gDimension,gFeet,detail::pathToUtf8(library()),gStatus,gSelection.filter()};
    if(!s.worldAvailable){s.report.reset();s.session.writable=false;}
    if(s.worldAvailable && gReport)if(auto target=gSelection.target(gReport->stamp))s.target=std::move(target->mismatch);
    s.phase=s.worldAvailable?gVerification.phase():VerificationPhase::NotVerified;
    return s;
}
void refreshFiles() {
    std::lock_guard lock(gMutex);
    try {
        std::filesystem::create_directories(library());
        std::vector<std::string> files; std::size_t visits{};
        for (auto const& e : std::filesystem::recursive_directory_iterator(library(),std::filesystem::directory_options::skip_permission_denied)) {
            if (++visits > 4096 || files.size() >= 512) break;
            if (!e.is_regular_file()) continue;
            auto rel = detail::pathToUtf8(e.path().lexically_relative(library()));
            std::replace(rel.begin(),rel.end(),'\\','/');
            if (safeSchematicPath(rel)) { (void)resolveSchematicPath(library(),rel); files.push_back(rel); }
        }
        std::sort(files.begin(),files.end()); gFiles = std::move(files); gStatus.clear();
    } catch (std::exception const& e) { gStatus = e.what(); }
}
bool place(std::string const& file) {
    std::lock_guard lock(gMutex);
    if (!contextValid() || !safeSchematicPath(file)) return false;
    auto snap = gSession.snapshot(); auto d = snap.document;
    if (d.placements.size() >= kMaxPlacements) return false;
    try { if (!std::filesystem::is_regular_file(resolveSchematicPath(library(),file))) return false; }
    catch (std::exception const& e) { gStatus=e.what(); return false; }
    std::uint64_t id=1; while(std::any_of(d.placements.begin(),d.placements.end(),[&](auto const& p){return p.id==id;}))++id;
    SavedPlacement p; p.id=id; p.name=file.size()<=128?file:"Schematic"; p.file=file; p.dimension=gDimension; p.origin=gFeet;
    d.placements.push_back(std::move(p)); d.selected=id;
    if (!change(snap,std::move(d))) return false;
    gActivate=true; return true;
}
bool select(std::uint64_t id) {
    std::lock_guard lock(gMutex);if(!contextValid())return false; auto snap=gSession.snapshot(); auto d=snap.document;
    if(!selectPlacement(d,id))return false; if (!change(snap,std::move(d))) return false; gActivate=true; return true;
}
bool importSavedProjection() {
    auto const legacy=detail::StructureSession::getInstance().savedProjection();
    if(!legacy.available)return false;
    std::lock_guard lock(gMutex);if(!contextValid())return false;
    auto snap=gSession.snapshot();auto d=snap.document;
    if(!snap.writable || d.placements.size()>=kMaxPlacements)return false;
    try {
        auto const source=detail::pathFromUtf8(legacy.structurePath);
        if(!std::filesystem::is_regular_file(source) || std::filesystem::file_size(source)>268435456)throw std::runtime_error("Schematic import size invalid");
        std::filesystem::create_directories(library());
        std::uint64_t id=1;while(std::any_of(d.placements.begin(),d.placements.end(),[&](auto const& p){return p.id==id;}))++id;
        auto file=detail::pathToUtf8(source.filename());
        if(!safeSchematicPath(file))throw std::runtime_error("Schematic file name invalid");
        // Never overwrite a user's library file during legacy migration.
        auto dest=resolveSchematicPath(library(),file);
        if(std::filesystem::weakly_canonical(source)!=dest){
            unsigned suffix{};
            while(std::filesystem::exists(dest)){
                file="import-"+std::to_string(++suffix)+"-"+detail::pathToUtf8(source.filename());
                dest=resolveSchematicPath(library(),file);
                if(suffix>512)throw std::runtime_error("No free schematic import name");
            }
            io::writeOutputAtomically(dest,[&](auto const& staged){return std::filesystem::copy_file(source,staged);});
        }
        auto p=migrateSavedPlacement(legacy,file,id,gDimension);
        d.placements.push_back(std::move(p));d.selected=id;
        if(!change(snap,std::move(d)))return false;gActivate=true;return true;
    }catch(std::exception const& e){gStatus=e.what();return false;}
}
bool erase(std::uint64_t id) {
    std::lock_guard lock(gMutex);if(!contextValid())return false; auto snap=gSession.snapshot(); auto d=snap.document;
    bool const active=d.selected==id;
    if(!removePlacement(d,id))return false;
    if(!change(snap,std::move(d)))return false; if(active)gActivate=true; return true;
}
bool edit(SavedPlacement const& placement,std::uint64_t revision) {
    std::lock_guard lock(gMutex);if(!contextValid())return false; auto snap=gSession.snapshot(); if(snap.revision!=revision)return false;
    auto d=snap.document; auto it=std::find_if(d.placements.begin(),d.placements.end(),[&](auto const& p){return p.id==placement.id;});
    if(it==d.placements.end())return false; if(*it==placement)return true;
    *it=placement; if(!change(snap,std::move(d)))return false;
    if(snap.document.selected==placement.id)gApply=true; return true;
}
bool moveToFeet(std::uint64_t id) {
    auto s=snapshot(); if(!s.worldAvailable)return false;
    for(auto p:s.session.document.placements)if(p.id==id){p.origin=s.feet;return edit(p,s.session.revision);} return false;
}
void verify(){
    auto const structure=detail::StructureSession::getInstance().snapshot();
    std::lock_guard lock(gMutex);
    auto const snap=gSession.snapshot();auto const p=selectedPlacement(snap.document);
    if(!contextValid() || !p || p->dimension!=gDimension || !snap.writable)return;
    auto const generation=!gActivate && !gApply && p->id==gActiveId
        && structure.loaded && structure.loaded->sourcePath==gActivePath
        && placementCurrent(stampFor(*p,snap.revision,structure.loaded->generation,1),structure)
        ?structure.loaded->generation:0;
    if(gVerification.request(stampFor(*p,snap.revision,generation,0))){retireReport();gStatus.clear();}
}
void cancelVerification(){
    std::lock_guard lock(gMutex);
    if(gVerification.busy()){gVerification.cancel();retireReport();}
}
bool selectMistake(ReportStamp const& stamp,std::size_t index) {
    auto const structure=detail::StructureSession::getInstance().snapshot();
    std::lock_guard lock(gMutex);
    if(!reportCurrent(structure)){retireReport();return false;}
    return gSelection.select(stamp,gReport->stamp,gReport->running,gReport->mismatches,index);
}
void setMistakeFilter(MistakeFilter filter){std::lock_guard lock(gMutex);setFilter(filter);}
void clearMistakeTarget(){std::lock_guard lock(gMutex);gSelection.clear();}
void cycleMistake(MistakeFilter filter) {
    auto const structure=detail::StructureSession::getInstance().snapshot();
    std::lock_guard lock(gMutex);
    if(!setFilter(filter))return;
    if(!reportCurrent(structure)){retireReport();return;}
    gSelection.cycle(gReport->stamp,gReport->running,gReport->mismatches,filter);
}
std::optional<SelectedMistake> highlightTarget(std::uint64_t worldEpoch,int dimension,
    std::uint64_t loadedGeneration) noexcept {
    try {
        std::lock_guard lock(gMutex);
        if(!gAvailable || worldEpoch!=gWorldEpoch || dimension!=gDimension || !gReport
            || loadedGeneration!=gReport->stamp.loadedGeneration)return {};
        return gSelection.target(gReport->stamp);
    }catch(...){return {};}
}
void reset(){std::lock_guard lock(gMutex);gSession.clear();gTransientDimensions.clear();gWorldKey.clear();gAvailable=false;gActivate=false;gApply=false;gWorldEpoch=0;gActiveId=0;gActivePath.clear();invalidateVerification();}
void shutdown(){gJob.reset();reset();}
void processControl() {
    std::optional<SavedPlacement> activation;
    bool apply{};
    {
        std::lock_guard lock(gMutex); if(!gActivate && !gApply)return;apply=gApply && !gActivate;gActivate=false;gApply=false;
        auto snap=gSession.snapshot(); if(contextValid())if(auto p=selectedPlacement(snap.document);p && p->dimension==gDimension)activation=*p;
        gActiveId=activation?activation->id:0;
    }
    try {
    if (apply && activation) {
        auto& session=detail::StructureSession::getInstance(); auto const snap=session.snapshot(); auto const& p=*activation;
        if(snap.loaded && snap.saved.available && snap.loaded->sourcePath==resolveSchematicPath(library(),p.file)) {
            session.applyTransform({p.rotation,p.mirror,
                static_cast<int>(p.origin.x-snap.saved.anchorX),static_cast<int>(p.origin.y-snap.saved.anchorY),static_cast<int>(p.origin.z-snap.saved.anchorZ),
                p.layerMode,p.layer,p.layerAxis,p.visible,p.countExtras});
            saveSettings(); return;
        }
    }
    clear(); // cancels old async load intents and drains through existing owner
    if(!activation)return;
        auto const& p=*activation;
        auto const resolved=resolveSchematicPath(library(),p.file);
        auto const path=detail::pathToUtf8(resolved);
        {std::lock_guard lock(gMutex);gActivePath=resolved;}
        detail::SavedProjectionSnapshot saved{true,static_cast<int>(p.origin.x),static_cast<int>(p.origin.y),static_cast<int>(p.origin.z),
            {p.rotation,p.mirror,0,0,0,p.layerMode,p.layer,p.layerAxis,p.visible,p.countExtras},path};
        detail::StructureSession::getInstance().setSavedProjection(saved);
        restoreSavedProjection();
    } catch(std::exception const& e){std::lock_guard lock(gMutex);gStatus=e.what();}
}
void rememberSelectedTransform() {
    auto const s=detail::StructureSession::getInstance().snapshot(); if(!s.loaded || !s.saved.available)return;
    std::lock_guard lock(gMutex);if(!contextValid())return; auto snap=gSession.snapshot(); auto d=snap.document;
    auto p=selectedPlacement(d); if(!p || p->id!=gActiveId)return;
    try {if(detail::pathFromUtf8(s.lastPath)!=resolveSchematicPath(library(),p->file))return;}catch(...){return;}
    auto it=std::find_if(d.placements.begin(),d.placements.end(),[&](auto const& v){return v.id==gActiveId;});
    it->origin={static_cast<std::int64_t>(s.saved.anchorX)+s.transform.offsetX,static_cast<std::int64_t>(s.saved.anchorY)+s.transform.offsetY,static_cast<std::int64_t>(s.saved.anchorZ)+s.transform.offsetZ};
    it->rotation=s.transform.rotation;it->mirror=s.transform.mirror;it->layerAxis=s.transform.layerAxis;
    it->layerMode=s.transform.layerDisplayMode;it->layer=s.transform.displayLayer;it->visible=s.transform.visible;it->countExtras=s.transform.countExtras;
    if(d!=snap.document)(void)change(snap,std::move(d));
}
void tick(LocalPlayer& player) {
    auto const view=capture::getClientViewSnapshot();
    if(!view){gJob.reset();std::lock_guard lock(gMutex);gAvailable=false;invalidateVerification();return;}
    auto const feet=player.getFeetPos(); auto const checked=projection::detail::checkedBlockCell({feet.x,feet.y,feet.z},1);
    auto const dim=static_cast<int>(player.getDimensionId());
    std::string key;
    {std::lock_guard lock(gMutex);if(view->worldEpoch==gWorldEpoch)key=gWorldKey;}
    if(key.empty() && !player.getLevel().isMultiplayerGame()) {
        try{key=placementWorldKey(player.getLevel().getLevelId());}catch(...){ }
    }
    bool const stable=!key.empty() && !key.starts_with("session-");
    // Temporary Level identity keeps in-memory server dimensions together.
    // It is compared as a value only and is never persisted or dereferenced.
    if(key.empty())key="session-"+std::to_string(reinterpret_cast<std::uintptr_t>(&player.getLevel()));
    PlacementSessionSnapshot snap;
    {
        std::lock_guard lock(gMutex);
        if(key!=gWorldKey || dim!=gDimension || view->worldEpoch!=gWorldEpoch){
            if(key!=gWorldKey || (dim==gDimension && view->worldEpoch!=gWorldEpoch))gTransientDimensions.clear();
            else if(!stable)gTransientDimensions.remember(gDimension,gSession.snapshot().document);
            gSession.clear();gWorldKey=key;gWorldEpoch=view->worldEpoch;gDimension=dim;invalidateVerification();gActivePath.clear();gActiveId=0;
            if(stable)gSession.bind(LHolo::getInstance().getSelf().getConfigDir()/"placements"/key/("dimension-"+std::to_string(dim)+".json"));
            else {
                gSession.bindTransient();
                if(auto cached=gTransientDimensions.find(dim))gSession.replace(std::move(*cached),gSession.snapshot().revision);
            }
            gStatus=stable?"":"Session only: stable server address+port unavailable.";
            gActivate=gSession.snapshot().document.selected!=0;
            gApply=false; // retire any old-world/dimension edit intent
        }
        gAvailable=checked.has_value(); if(checked)gFeet={(*checked)[0],(*checked)[1],(*checked)[2]};
        snap=gSession.snapshot();
    }
    auto const p=selectedPlacement(snap.document);auto const structure=detail::StructureSession::getInstance().snapshot();auto const loaded=structure.loaded;
    if(!p || p->dimension!=dim || !checked){gJob.reset();std::lock_guard lock(gMutex);invalidateVerification();return;}
    if(!loaded){
        gJob.reset();std::lock_guard lock(gMutex);
        if(gVerification.phase()==VerificationPhase::Running ||
            (gVerification.busy() && !gVerification.belongsTo(stampFor(*p,snap.revision,0,0))))invalidateVerification();
        return;
    }
    std::optional<SelectedMistake> selected;
    {std::lock_guard lock(gMutex);
        if(gActivate || gApply || p->id!=gActiveId){
            gJob.reset();
            if(gVerification.phase()!=VerificationPhase::Queued)invalidateVerification();
            return;
        }
        if(loaded->sourcePath!=gActivePath || !placementCurrent(stampFor(*p,snap.revision,loaded->generation,1),structure)){
            gJob.reset();
            if(gVerification.phase()!=VerificationPhase::Queued ||
                !gVerification.belongsTo(stampFor(*p,snap.revision,loaded->generation,0)))invalidateVerification();
            return;
        }
        if(gVerification.busy() && !gVerification.belongsTo(stampFor(*p,snap.revision,loaded->generation,0)))invalidateVerification();
        if(gReport && !reportCurrent(structure))invalidateVerification();
        if(gReport)selected=gSelection.target(gReport->stamp);
    }
    // Keep a selected marker fresh while the menu is closed. One bounded
    // native cell check uses an existing tick-budget slot, never a UI/render
    // callback or a new scan. Native reads happen outside all owner locks.
    bool const selectedRead=selected.has_value();
    if(selected){
        bool changed=true;
        try {
            auto const& row=selected->mismatch;
            BlockPos const pos{static_cast<int>(row.world.x),static_cast<int>(row.world.y),static_cast<int>(row.world.z)};
            auto& source=player.getDimensionBlockSource();
            if(source.areChunksFullyLoaded(pos,0)){
                auto const& actual=row.actualExtra?source.getExtraBlock(pos):row.actualLiquid?source.getLiquidBlock(pos):source.getBlock(pos);
                changed=placeholderBlock(actual.getTypeName()) || describe(&actual)!=row.actual;
            }
        }catch(...){changed=true;}
        if(changed){std::lock_guard lock(gMutex);gSelection.clearIfCurrent(*selected);}
    }
    {
        std::lock_guard lock(gMutex);
        if(gJob && (!gVerification.current(gJob->requestSerial,stampFor(*p,snap.revision,loaded->generation,0))
            || gJob->loaded!=loaded))gJob.reset();
    }
    if(!gJob){
        std::lock_guard lock(gMutex);
        if(gVerification.phase()!=VerificationPhase::Queued)return;
    }
    if(!gJob){
        Job j;j.loaded=loaded;j.placement=*p;j.revision=snap.revision;j.observer=checked ? Cell{(*checked)[0],(*checked)[1],(*checked)[2]} : p->origin;
        j.regionWork.push_back(0);
        for(auto const& region:loaded->regions){
            auto const volume=static_cast<double>(std::max(0,region.sizeX))*std::max(0,region.sizeY)*std::max(0,region.sizeZ);
            j.regionWork.push_back(j.regionWork.back()+volume);
        }
        j.totalWork=static_cast<double>(loaded->renderBlocks.size())+j.regionWork.back();
        j.transform={{loaded->sizeX,loaded->sizeY,loaded->sizeZ},p->origin,p->rotation,p->mirror};
        auto const size=j.transform.placedSize();
        if(!projection::detail::checkedProjectionOrigin({static_cast<int>(p->origin.x),static_cast<int>(p->origin.y),static_cast<int>(p->origin.z)},{0,0,0},{static_cast<int>(size.x),static_cast<int>(size.y),static_cast<int>(size.z)},0)){
            std::lock_guard lock(gMutex);invalidateVerification();gStatus="Verification placement is outside world bounds.";return;
        }
        j.rotation=projection::detail::getProjectionRotation(p->rotation);
        j.mirror=projection::detail::getProjectionMirror(p->mirror);
        detail::StructureSession::getInstance().publishVerificationIfCurrent(structure,[&]{
            std::lock_guard lock(gMutex);
            if(gSession.snapshot().revision!=snap.revision || gWorldEpoch!=view->worldEpoch || !contextValid())return false;
            if(++gReportRevision==0)++gReportRevision;
            j.result.stamp=stampFor(*p,snap.revision,loaded->generation,gReportRevision);
            j.requestSerial=gVerification.start(j.result.stamp);
            if(!j.requestSerial)return false;
            j.result.running=true;gJob.emplace(std::move(j));
            gReport=std::make_shared<Report const>(gJob->result);
            return true;
        });
    }
    if(!gJob)return;
    auto& job=*gJob;
    // Finite game-tick work, completely outside mesh/render/upload paths.
    constexpr std::size_t budget=256;
    std::size_t remaining=budget-static_cast<std::size_t>(selectedRead);
    while(remaining){
        if(job.block<loaded->renderBlocks.size()){--remaining;scanBlock(job,player);}
        else if(job.air.region<loaded->regions.size())scanAir(job,player,remaining);
        else break;
    }
    {
        auto progress=std::make_shared<Report>();progress->running=true;progress->checked=job.result.checked;progress->stamp=job.result.stamp;
        auto const done=static_cast<double>(job.block)+job.regionWork[std::min(job.air.region,loaded->regions.size())]+job.air.index;
        progress->progress=job.totalWork>0?static_cast<float>(std::clamp(done/job.totalWork,0.,1.)):1.f;
        auto const published=detail::StructureSession::getInstance().publishVerificationIfCurrent(structure,[&]{
            std::lock_guard lock(gMutex);
            if(!contextValid() || !gVerification.current(job.requestSerial,job.result.stamp))return false;
            progress->stamp.filterRevision=gFilterRevision;gReport=std::move(progress);return true;
        });
        if(!published){
            std::lock_guard lock(gMutex);
            if(gVerification.current(job.requestSerial,job.result.stamp))invalidateVerification();
            gJob.reset();return;
        }
    }
    if(job.block==loaded->renderBlocks.size() && job.air.region==loaded->regions.size()){
        if(!job.finalizing){
            for(auto& rows:job.nearest)for(auto& row:rows)job.result.mismatches.push_back(std::move(row));
            std::sort(job.result.mismatches.begin(),job.result.mismatches.end(),nearerMismatch);
            job.finalizing=true;job.materialCursor=job.materials.begin();
            job.inventory=countInventoryItems(player.getInventory());
        }
        while(remaining && job.materialCursor!=job.materials.end()){
            --remaining;auto const& [item,count]=*job.materialCursor++;
            job.result.materials.push_back({item,count,item.starts_with("unmapped: ")?std::nullopt:std::optional<int>{job.inventory[item]}});
        }
        if(job.materialCursor!=job.materials.end())return;
        job.result.running=false;
        job.result.progress=1.f;
        auto const published=detail::StructureSession::getInstance().publishVerificationIfCurrent(structure,[&]{
            std::lock_guard lock(gMutex);
            if(!contextValid() || !gVerification.finish(job.requestSerial,job.result.stamp))return false;
            job.result.stamp.filterRevision=gFilterRevision;
            gSelection.reconcile(job.result.stamp,job.result.mismatches);
            gReport=std::make_shared<Report const>(std::move(job.result));return true;
        });
        if(!published){std::lock_guard lock(gMutex);if(gVerification.current(job.requestSerial,job.result.stamp))invalidateVerification();}
        gJob.reset();
    }
}
} // namespace lholo::structure::schematic
