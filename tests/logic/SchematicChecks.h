#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "structure/PlacementSession.h"
#include "structure/Verification.h"
#include "structure/VerificationSelection.h"
#include "structure/NativeBlockTransform.h"
#include "structure/PlacementMigration.h"
#include "structure/InventoryCountRules.h"
#include "structure/TransientPlacementCache.h"
#include "settings/SettingsStore.h"
#include "projection/core/ProjectionRules.h"
#include "projection/runtime/ChunkAvailabilityQueue.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <tuple>

namespace lholo::tests {
template<class Check> void runSchematicChecks(Check check) {
    using namespace structure;
    using namespace projection::detail;
    auto throws=[&](auto&& operation){try{operation();return false;}catch(...){return true;}};
    check(inventoryCountAfterStack(32,64)==96);
    check(inventoryCountAfterStack(32,0)==32);
    check(inventoryCountAfterStack(32,-1)==32);
    check(inventoryCountAfterStack(-1,64)==64);
    check(inventoryCountAfterStack(INT_MAX-1,64)==INT_MAX);
    check(inventoryCountAfterStack(INT_MAX,1)==INT_MAX);
    check(isShulkerInventoryContainer("shulker_box"));
    check(isShulkerInventoryContainer("minecraft:shulker_box"));
    for (auto color : {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                       "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"}) {
        auto const id = std::string(color) + "_shulker_box";
        check(isShulkerInventoryContainer(id));
        check(isShulkerInventoryContainer("minecraft:" + id));
    }
    for (auto id : {"", "minecraft:", "shulker", "minecraft:shulker_shell", "_shulker_box",
                    "minecraft:_shulker_box", "minecraft:red_shulker_boxes", "shulker_box_extra",
                    "minecraft:red_shulker_box_extra", "minecraft:shulker_box_spawn_egg",
                    "minecraft:shulker_box ", "other:shulker_box"})
        check(!isShulkerInventoryContainer(id));
    ShulkerInventorySlotState shulkerSlots{};
    auto const emptySlots = shulkerSlots;
    for (auto const [slot, count] : {std::pair{-1, 1}, {27, 1}, {INT_MIN, 1}, {INT_MAX, 1},
                                   {0, 0}, {0, -1}, {26, 128}, {0, INT_MAX}}) {
        check(!claimShulkerInventorySlot(shulkerSlots, slot, count));
        check(shulkerSlots == emptySlots);
    }
    for (int slot = 0; slot < 27; ++slot) {
        check(claimShulkerInventorySlot(shulkerSlots, slot, slot == 26 ? 127 : 1));
        auto const beforeDuplicate = shulkerSlots;
        check(!claimShulkerInventorySlot(shulkerSlots, slot, 64));
        check(shulkerSlots == beforeDuplicate);
    }
    check(inventoryCountAfterStack(INT_MAX - 127, 127) == INT_MAX);
    check(inventoryCountAfterStack(INT_MAX - 126, 127) == INT_MAX);
    int shulkerTotal = INT_MAX - 127;
    for (int slot = 0; slot < 27; ++slot)
        shulkerTotal = inventoryCountAfterStack(shulkerTotal, 127);
    check(shulkerTotal == INT_MAX);
    for(int y=-1024;y<=1024;++y) {
        auto const sub=ChunkAvailabilityQueue::subChunkY(y);
        check(sub*16<=y && (sub+1)*16>y);
    }
    ChunkAvailabilityQueue availability;
    availability.push(1,2,-64,319);availability.push(1,2,-48,32);
    check(availability.take(0,128,[](auto const&){return true;}).empty());
    check(availability.take(1,0,[](auto const&){return true;}).empty());
    std::vector<std::array<int,3>> column;
    while(!availability.empty()) {
        auto const part=availability.take(1,1,[](auto const&){return true;});
        check(part.size()==1);column.insert(column.end(),part.begin(),part.end());
    }
    check(column.size()==24);
    for(int i=0;i<24;++i)check(column[static_cast<std::size_t>(i)]==std::array{1,i-4,2});
    availability.push(1,2,-1000000,1000000);
    int inspected{};
    check(availability.take(8,128,[&](auto const&){++inspected;return false;}).empty());
    check(inspected==128 && !availability.empty());availability.clear();check(availability.empty());
    availability.push(1,2,5,-5);check(availability.empty());
    WorldEventInterest const columns{{{{16,-64,32},{32,320,48}}}};
    check(columns.intersectsChunkColumn(1,2,-64,319));
    check(!columns.intersectsChunkColumn(2,2,-64,319));
    check(!columns.intersectsChunkColumn(1,3,-64,319));
    check(!columns.intersectsChunkColumn(1,2,320,1000));
    check(!columns.intersectsChunkColumn(1,2,10,-10));
    // Exhaustive inverses, all cells and neighbor ring, origin/turned box
    // contract, asymmetric sizes, all 12 rotation/mirror combinations.
    for(int sx=1;sx<=4;++sx)for(int sy=1;sy<=3;++sy)for(int sz=1;sz<=5;++sz)
    for(int r=0;r<4;++r)for(int m=0;m<3;++m){
        PlacementTransform t{{sx,sy,sz},{-27,-64,31},r,m};
        auto const size=t.placedSize();
        std::set<std::tuple<std::int64_t,std::int64_t,std::int64_t>> cells;
        LoadedStructure loaded;loaded.sizeX=sx;loaded.sizeY=sy;loaded.sizeZ=sz;
        for(int x=-1;x<=sx;++x)for(int y=-1;y<=sy;++y)for(int z=-1;z<=sz;++z){
            Cell const local{x,y,z};auto const world=t.toWorld(local);auto const back=t.toLocal(world);
            check(back==local);
            bool const inside=x>=0 && x<sx && y>=0 && y<sy && z>=0 && z<sz;
            check(t.contains(world)==inside);
            check(inside==(world.x>=t.origin.x && world.x<t.origin.x+size.x && world.y>=t.origin.y && world.y<t.origin.y+size.y && world.z>=t.origin.z && world.z<t.origin.z+size.z));
            auto const native=transformStructurePosition(BlockPos{x,y,z},loaded,m,r);
            check(native.x==world.x-t.origin.x && native.y==world.y-t.origin.y && native.z==world.z-t.origin.z);
            if(inside)cells.emplace(world.x,world.y,world.z);
            for(int a=3;a<=8;++a){
                auto const axis=layerAxisFromInt(a);auto const rank=layerOf(t,local,axis);
                auto const offset=Cell{world.x-t.origin.x,world.y-t.origin.y,world.z-t.origin.z};
                int const want=a==3?static_cast<int>(offset.y):a==4?static_cast<int>(size.y-1-offset.y)
                    :a==5?static_cast<int>(offset.x):a==6?static_cast<int>(size.x-1-offset.x)
                    :a==7?static_cast<int>(offset.z):static_cast<int>(size.z-1-offset.z);
                check(rank==want);
                if(inside){check(rank>=0 && rank<layerCount(size,axis));
                    check(isLayerVisible(rank,LayerDisplayMode::All,0,-1,-1,axis));
                    check(isLayerVisible(rank,LayerDisplayMode::Single,want,-1,-1,axis));
                    check(!isLayerVisible(rank,LayerDisplayMode::Single,want+1,-1,-1,axis));
                    check(isLayerVisible(rank,LayerDisplayMode::UpToCurrent,want,-1,-1,axis));
                    check(!isLayerVisible(rank,LayerDisplayMode::UpToCurrent,want-1,-1,-1,axis));
                }
            }
            check(layerOf(t,local,LayerAxis::X)==x);check(layerOf(t,local,LayerAxis::Y)==y);
        }
        check(cells.size()==static_cast<std::size_t>(sx*sy*sz));
    }
    check(toInt(LayerAxis::Y)==0 && toInt(LayerAxis::X)==1 && toInt(LayerAxis::Material)==2);
    check(toInt(LayerDisplayMode::FromCurrent)==3);
    // Regression dispatch for facing-sensitive families. No hand-written
    // orientation math: record the exact native transformer arguments/result.
    struct FakeBlock{std::string_view name;};FakeBlock result{"transformed"};
    for(auto name:{"stairs","hopper","trapdoor","redstone_torch","repeater"}){
        FakeBlock source{name};
        for(int r=0;r<4;++r)for(int m=0;m<3;++m){
            int calls{};auto const rotation=getProjectionRotation(r);auto const mirror=getProjectionMirror(m);
            auto native=[&](FakeBlock const& b,Rotation turn,Mirror flip){++calls;check(b.name==name);check(turn==rotation && flip==mirror);return &result;};
            check(nativeBlockTransform(&source,false,rotation,mirror,native)==&result);check(calls==1);
            check(nativeBlockTransform(&source,true,rotation,mirror,native)==&source);check(calls==1);
            check(nativeBlockTransform<FakeBlock>(nullptr,false,rotation,mirror,native)==nullptr);check(calls==1);
        }
    }
    auto& transformSession=detail::StructureSession::getInstance();
    detail::StructureTransformSnapshot transformValue{2,1,7,-3,5,LayerDisplayMode::Single,2,LayerAxis::NorthToSouth,false,false};
    check(transformSession.applyTransform(transformValue));check(transformSession.transform()==transformValue);
    check(!transformSession.applyTransform(transformValue));transformSession.resetTransform();
    using V=VerificationState;
    for(int mask=0;mask<128;++mask){
        bool covered=mask&1,ready=mask&2,air=mask&4,actualAir=mask&8,type=mask&16,state=mask&32,extras=mask&64;
        auto const v=classifyCell(covered,ready,air,actualAir,type,state,extras);
        auto const wanted=!covered?V::Ignored:!ready?V::Unknown:air?(actualAir?V::Correct:extras?V::Extra:V::Ignored)
            :actualAir?V::Missing:!type?V::WrongType:state?V::Correct:V::WrongState;
        check(v==wanted);
    }
    check(placeholderBlock("minecraft:unknown"));check(placeholderBlock("minecraft:info_update"));check(placeholderBlock("minecraft:info_update2"));check(!placeholderBlock("minecraft:stone"));
    for(auto a:{V::Correct,V::Missing,V::WrongType,V::WrongState,V::Unknown})for(auto b:{V::Correct,V::Missing,V::WrongType,V::WrongState,V::Unknown}){
        check(combineExpectedStates(a,b)==combineExpectedStates(b,a));
        if(a==V::Unknown || b==V::Unknown)check(combineExpectedStates(a,b)==V::Unknown);
        else if(a==V::Missing || b==V::Missing)check(combineExpectedStates(a,b)==V::Missing);
    }
    VerificationTally tally;
    for(auto state:{V::Correct,V::Missing,V::WrongType,V::WrongState,V::Unknown,V::Extra,V::Ignored})tally.add(state,true);
    tally.add(V::Correct,false);tally.add(V::Unknown,false);
    check(tally.total()==5 && tally.correct==1 && tally.extra==1 && tally.unknown==1 && tally.unknownAir==1);
    for(int kind=0;kind<7;++kind){
        auto v=static_cast<V>(kind);check(matchesFilter(v,MistakeFilter::Missing)==(v==V::Missing));
        check(matchesFilter(v,MistakeFilter::WrongState)==(v==V::WrongState));
        check(matchesFilter(v,MistakeFilter::WrongAndExtra)==(v==V::WrongType || v==V::Extra));
        check(matchesFilter(v,MistakeFilter::WrongType)==(v==V::WrongType));
        check(matchesFilter(v,MistakeFilter::Extra)==(v==V::Extra));
    }
    check(static_cast<int>(MistakeFilter::Missing)==3 && static_cast<int>(MistakeFilter::WrongType)==4 && static_cast<int>(MistakeFilter::Extra)==5);
    check(tally.wrongType==1 && tally.extra==1); // Full tallies are not capped list sizes.
    check(!matchesFilter(V::Missing,static_cast<MistakeFilter>(99)));
    std::vector<Mismatch> rows;
    for(int i=1000;i>=0;--i)retainNearest(rows,{V::Missing,{i,0,0},{},{},static_cast<double>(i*i)},8);
    check(rows.size()==8);std::sort(rows.begin(),rows.end(),nearerMismatch);
    for(int i=0;i<8;++i)check(rows[static_cast<std::size_t>(i)].world.x==i);
    check(nextMistake(rows,MistakeFilter::Missing,{})==0);check(nextMistake(rows,MistakeFilter::Missing,7)==0);
    check(!nextMistake(rows,MistakeFilter::WrongAndExtra,{}));check(!nextMistake(std::vector<Mismatch>{},MistakeFilter::Mistakes,{}));
    {
        using namespace structure::schematic;
        ReportStamp stamp{12,0,7,3,42,100,{10,64,-8},1,2,LayerAxis::Y,LayerDisplayMode::All,0,true,true,0};
        std::vector<Mismatch> errors{
            {V::Missing,{11,65,-7},"stairs[facing=west]","minecraft:air",1},
            {V::WrongType,{12,65,-7},"minecraft:stone","minecraft:dirt",4},
            {V::WrongState,{13,65,-7},"stairs[facing=west]","stairs[facing=east]",9},
            {V::Extra,{14,65,-7},"minecraft:air","minecraft:stone",16}
        };
        MistakeSelection selection;
        check(selection.select(stamp,stamp,false,errors,2));
        auto target=selection.target(stamp);check(target && target->index==2 && target->mismatch.expected==errors[2].expected);
        auto stale=stamp;--stale.reportRevision;
        check(!selection.select(stale,stamp,false,errors,0));
        check(!selection.select(stamp,stamp,true,errors,0));
        check(!selection.select(stamp,stamp,false,errors,errors.size()));
        check(selection.target(stamp)->index==2); // Rejected events cannot alter the valid target.
        check(selection.setFilter(MistakeFilter::WrongType));check(!selection.target(stamp));
        check(!selection.select(stamp,stamp,false,errors,0));
        check(selection.select(stamp,stamp,false,errors,1));
        check(!selection.select(stamp,stamp,false,errors,3));
        check(!selection.setFilter(static_cast<MistakeFilter>(99)));check(selection.filter()==MistakeFilter::WrongType);
        check(selection.setFilter(MistakeFilter::Extra));check(!selection.target(stamp));
        check(selection.cycle(stamp,false,errors,MistakeFilter::Extra));check(selection.target(stamp)->index==3);
        check(selection.cycle(stamp,false,errors,MistakeFilter::Missing));check(selection.target(stamp)->index==0);
        check(selection.cycle(stamp,false,errors,MistakeFilter::Mistakes));check(selection.target(stamp)->index==0);
        check(selection.cycle(stamp,false,errors,MistakeFilter::Mistakes));check(selection.target(stamp)->index==1);
        selection.clear();check(!selection.target(stamp));

        // Every context field participates in selection retirement, even if a
        // new load/job reused the same list length and matching coordinates.
        auto contextChange=[&](auto change){
            auto changed=stamp;change(changed);
            check(selection.select(stamp,stamp,false,errors,0));
            check(!selection.target(changed));
            check(!selection.select(stamp,changed,false,errors,0));
            selection.reconcile(changed,errors);check(!selection.target(changed));
        };
        contextChange([](auto& s){++s.worldEpoch;});
        contextChange([](auto& s){++s.dimension;});
        contextChange([](auto& s){++s.sessionRevision;});
        contextChange([](auto& s){++s.placementId;});
        contextChange([](auto& s){++s.loadedGeneration;});
        contextChange([](auto& s){++s.placementOrigin.x;});
        contextChange([](auto& s){++s.placementOrigin.y;});
        contextChange([](auto& s){++s.placementOrigin.z;});
        contextChange([](auto& s){++s.placementRotation;});
        contextChange([](auto& s){++s.placementMirror;});
        contextChange([](auto& s){s.layerAxis=LayerAxis::X;});
        contextChange([](auto& s){s.layerMode=LayerDisplayMode::Single;});
        contextChange([](auto& s){++s.layer;});
        contextChange([](auto& s){s.visible=false;});
        contextChange([](auto& s){s.countExtras=false;});
        contextChange([](auto& s){++s.filterRevision;});

        check(selection.select(stamp,stamp,false,errors,2));
        auto next=stamp;++next.reportRevision;
        auto reordered=errors;std::reverse(reordered.begin(),reordered.end());reordered[1].distanceSquared=100;
        selection.reconcile(next,reordered);
        target=selection.target(next);check(target && target->index==1 && target->stamp==next && target->mismatch.distanceSquared==100);
        check(!selection.select(stamp,next,false,reordered,2)); // Old UI row event after refresh.
        auto changedRow=[&](auto change){
            check(selection.select(stamp,stamp,false,errors,2));
            auto changed=errors;change(changed[2]);selection.reconcile(next,changed);check(!selection.target(next));
        };
        changedRow([](auto& m){m.kind=V::WrongType;});
        changedRow([](auto& m){++m.world.x;});
        changedRow([](auto& m){m.expected="stairs[facing=south]";});
        changedRow([](auto& m){m.actual="stairs[facing=west]";});
        changedRow([](auto& m){m.actualLiquid=true;});
        changedRow([](auto& m){m.actualExtra=true;});
        // A native check begun before a UI selection/report refresh must not
        // clear the replacement selection, even at the same coordinates.
        check(selection.select(stamp,stamp,false,errors,2));
        auto const checkedTarget=*selection.target(stamp);
        check(selection.select(stamp,stamp,false,errors,1));
        check(!selection.clearIfCurrent(checkedTarget));check(selection.target(stamp)->index==1);
        check(selection.select(next,next,false,errors,2));
        check(!selection.clearIfCurrent(checkedTarget));check(selection.target(next)->stamp==next);
        auto checked=*selection.target(next);checked.mismatch.actualLiquid=true;
        check(!selection.clearIfCurrent(checked));
        checked=*selection.target(next);checked.mismatch.actualExtra=true;
        check(!selection.clearIfCurrent(checked));
        check(selection.clearIfCurrent(*selection.target(next)));check(!selection.target(next));
        check(selection.select(stamp,stamp,false,errors,2));
        selection.reconcile(next,{});check(!selection.target(next)); // Resolved or out of retained result.
        auto invalid=stamp;invalid.loadedGeneration=0;
        check(!selection.select(invalid,invalid,false,errors,0));
        check(!selection.cycle(stamp,true,errors,MistakeFilter::Mistakes));
    }
    std::vector<Mismatch> equal{{V::WrongType,{1,2,3},{},{},9},{V::Missing,{-1,2,3},{},{},9},{V::WrongState,{-1,1,3},{},{},9}};
    std::sort(equal.begin(),equal.end(),nearerMismatch);check(equal[0].world==Cell{-1,1,3});check(equal[1].world==Cell{-1,2,3});
    check(requiredItemRule("minecraft:wall_torch").alias=="minecraft:torch");
    check(requiredItemRule("minecraft:redstone_wall_torch").alias=="minecraft:redstone_torch");
    check(requiredItemRule("minecraft:soul_wall_torch").alias=="minecraft:soul_torch");
    for(auto name:{"minecraft:double_stone_block_slab","minecraft:oak_double_slab","minecraft:double_wooden_slab"})check(requiredItemRule(name).quantity==2);
    check(requiredItemRule("minecraft:wooden_door",true,false,false).quantity==1);
    check(requiredItemRule("minecraft:bed",false,true,false).quantity==1);
    MaterialCount door;door.add(requiredItemRule("minecraft:wooden_door",false).quantity,V::Correct);door.add(requiredItemRule("minecraft:wooden_door",true).quantity,V::Correct);
    check(door.total==1 && door.correct==1 && door.remaining()==0);
    MaterialCount bed;bed.add(requiredItemRule("minecraft:bed",false,false).quantity,V::Missing);bed.add(requiredItemRule("minecraft:bed",false,true).quantity,V::Missing);
    check(bed.total==1 && bed.remaining()==1);
    MaterialCount slab;slab.add(2,V::Unknown);slab.add(2,V::Correct);check(slab.total==4 && slab.correct==2 && slab.remaining()==2);
    // Bounded region cursor, overlapping regions, empty boxes, gaps. A one
    // operation budget must still converge to exactly the union of cells.
    std::vector<LoadedStructure::RegionBox> boxes{{0,0,0,2,2,2},{1,1,1,2,2,2},{9,0,0,1,1,1},{0,0,0,0,1,1}};
    std::set<std::tuple<std::int64_t,std::int64_t,std::int64_t>> unionCells;
    RegionScanCursor cursor;std::size_t turns{};
    while(cursor.region<boxes.size() && turns++<1000){std::size_t budget=1;auto p=cursor.next(boxes,budget);check(budget==0);if(p)check(unionCells.emplace(p->x,p->y,p->z).second);}
    check(turns<1000 && unionCells.size()==16);check(!unionCells.contains({4,0,0}));
    check(safeSchematicPath("folder/test.mcstructure"));check(safeSchematicPath("日本語/家.litematic"));check(safeSchematicPath("HOUSE.MCSTRUCTURE"));
    for(auto bad:{"","../x.mcstructure","a/../b.litematic","/x.mcstructure","C:/x.mcstructure","C:x.mcstructure","\\\\server/x.mcstructure","a\\x.litematic","a//b.litematic","a/./b.litematic","CON.mcstructure","com1.mcstructure","a./x.litematic","x.litematic/","x.json","x.mcstructure:stream"})check(!safeSchematicPath(bad));
    check(placementWorldKey("a")!=placementWorldKey("A"));
    check(placementServerKey("EXAMPLE.COM",19132)==placementServerKey("example.com",19132));
    check(placementServerKey("example.com",19132)!=placementServerKey("example.com",19133));
    check(throws([&]{placementWorldKey("");}));check(throws([&]{placementServerKey("host",0);}));
    PlacementDocument d;SavedPlacement p;p.id=1;p.name="家";p.file="home.litematic";p.origin={-25,65,81};p.dimension=2;p.rotation=3;p.mirror=2;p.layerAxis=LayerAxis::SouthToNorth;p.layerMode=LayerDisplayMode::UpToCurrent;p.layer=3;p.visible=false;p.countExtras=false;
    d.placements.push_back(p);d.selected=1;check(decodePlacements(encodePlacements(d))==d);
    TransientPlacementCache dimensions;dimensions.remember(0,d);
    auto nether=d;nether.placements[0].dimension=1;nether.selected=0;dimensions.remember(1,nether);
    check(dimensions.find(0)==d);check(dimensions.find(1)==nether);check(!dimensions.find(2));
    for(int dim=2;dim<40;++dim)dimensions.remember(dim,{});
    check(dimensions.size()==16 && !dimensions.find(0) && dimensions.find(39).has_value());
    dimensions.clear();check(dimensions.size()==0 && !dimensions.find(39));
    for(int axis=0;axis<=8;++axis)for(int mode=0;mode<4;++mode){auto round=d;round.placements[0].layerAxis=layerAxisFromInt(axis);round.placements[0].layerMode=layerDisplayModeFromInt(mode);check(decodePlacements(encodePlacements(round))==round);}
    auto multiple=d;auto second=p;second.id=2;second.origin={-25,65,82};multiple.placements.push_back(second);
    check(selectedPlacement(multiple)->id==1);check(!selectPlacement(multiple,99));check(multiple.selected==1);
    check(selectPlacement(multiple,2));check(removePlacement(multiple,1));check(multiple.selected==2);
    check(removePlacement(multiple,2));check(multiple.selected==0 && !selectedPlacement(multiple));
    detail::SavedProjectionSnapshot legacy;legacy.available=true;legacy.anchorX=-20;legacy.anchorY=64;legacy.anchorZ=17;
    legacy.transform={3,2,4,-3,9,LayerDisplayMode::FromCurrent,8,LayerAxis::Material,false,false};
    auto const migrated=migrateSavedPlacement(legacy,"legacy.mcstructure",9,2);
    check(migrated.origin==Cell{-16,61,26} && migrated.rotation==3 && migrated.mirror==2);
    check(migrated.layerAxis==LayerAxis::Material && migrated.layerMode==LayerDisplayMode::FromCurrent && migrated.layer==8);
    check(!migrated.visible && !migrated.countExtras && migrated.dimension==2);

    auto decoded=decodePlacements(R"({"version":1,"placements":[{"file":"a.litematic"}],"selected":99,"future":true})");
    check(decoded.selected==0 && decoded.placements[0].layerAxis==LayerAxis::BottomToTop && decoded.placements[0].visible);
    for(auto bad:{"{}","[]","{",R"({"version":2,"placements":[]})",R"({"version":1,"placements":[{"file":"../a.litematic"}]})",R"({"version":1,"placements":[{"id":1,"file":"a.litematic"},{"id":1,"file":"b.litematic"}]})",R"({"version":1,"placements":[{"file":"a.litematic","origin":[18446744073709551615,0,0]}]})"})check(throws([&]{decodePlacements(bad);}));
    check(throws([&]{decodePlacements(std::string(kMaxPlacementDocument+1,' '));}));
    check(throws([&]{decodePlacements(std::string(100,'[')+std::string(100,']'));}));
    auto excessive=d;excessive.placements.resize(kMaxPlacements+1);check(throws([&]{encodePlacements(excessive);}));
    auto const dir=std::filesystem::temp_directory_path()/("lholo-schematic-test-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    auto const path=dir/"placements.json";
    auto const legacyConfig=dir/"legacy-config.json";
    {std::ofstream out(legacyConfig);out<<R"({"version":13,"savedLayerAxis":2,"savedLayerDisplayMode":3})";}
    settings::Settings oldSettings;
    check(settings::loadSettingsFile(legacyConfig,oldSettings));
    check(oldSettings.savedLayerAxis==2 && oldSettings.savedLayerDisplayMode==3);
    check(oldSettings.savedVisible && oldSettings.savedCountExtras);
    oldSettings.savedVisible=false;oldSettings.savedCountExtras=false;
    for(int axis=0;axis<=8;++axis){
        oldSettings.savedLayerAxis=axis;settings::saveSettingsFile(legacyConfig,oldSettings);
        settings::Settings round;check(settings::loadSettingsFile(legacyConfig,round));
        check(round.savedLayerAxis==axis && !round.savedVisible && !round.savedCountExtras);
    }
    // Exclusively owned test files; keep fixture/error cases within this dir.
    std::filesystem::remove(path);
    PlacementSession session;session.bind(path);auto initial=session.snapshot();check(initial.writable);
    check(session.replace(d,initial.revision));check(!session.replace({},initial.revision));
    PlacementSession reread;reread.bind(path);check(reread.snapshot().document==d);
    auto current=session.snapshot();auto unselected=current.document;unselected.selected=0;check(session.replace(unselected,current.revision));check(session.snapshot().document.selected==0);
    session.bind(dir/"other-dimension.json");check(session.snapshot().document.placements.empty());
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<"broken document";}
    PlacementSession broken;broken.bind(path);auto error=broken.snapshot();check(!error.writable && !error.status.empty());check(!broken.replace(d,error.revision));
    {std::ifstream in(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});check(text=="broken document");}
    broken.bind(path);check(!broken.snapshot().writable);
    broken.bindTransient();check(broken.replace(d,broken.snapshot().revision));check(broken.snapshot().document==d);
    auto const root=dir/"schematics";std::filesystem::create_directories(root);
    check(resolveSchematicPath(root,"a.mcstructure")==std::filesystem::weakly_canonical(root/"a.mcstructure"));
    check(throws([&]{resolveSchematicPath(root,"../a.mcstructure");}));
    std::filesystem::remove(path);
    std::filesystem::remove(legacyConfig);
    std::filesystem::remove(dir/"other-dimension.json");std::filesystem::remove(root);std::filesystem::remove(dir);
}
} // namespace lholo::tests
