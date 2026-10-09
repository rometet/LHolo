// State/comparison and supplied-predictor coverage only. All native cells in
// the generated matrix remain NOT_RUN, including server apply/consumption.
#pragma once
struct FamilyCase {
    Block ghost,predicted;std::string control,wrong;std::optional<Block> upper;
};
inline FamilyCase makeFamilyCase(std::string name,std::string key,std::string value,std::string wrong,
    std::string post={}) {
    FamilyCase c;c.ghost.name=std::move(name);c.ghost.mNetworkId=30;c.ghost.states[key]=std::move(value);
    c.control=std::move(key);c.wrong=std::move(wrong);
    if(!post.empty()) c.ghost.states[post]="1";
    c.predicted=c.ghost;c.predicted.mNetworkId=31;
    if(!post.empty()) c.predicted.states[post]="0";
    return c;
}
inline std::vector<FamilyCase> familyCases(std::string_view family) {
    std::vector<FamilyCase> result;
    if(family=="hopper"||family=="piston"||family=="observer"||family=="dispenser"||family=="dropper") {
        std::string post;
        if(family=="hopper") post="toggle_bit";
        if(family=="observer") post="powered_bit";
        if(family=="dispenser"||family=="dropper") post="triggered_bit";
        for(auto key:{"facing_direction","minecraft:facing_direction"}) for(int f=0;f<6;++f) {
            if(family=="hopper"&&f==1) continue;
            auto c=makeFamilyCase("minecraft:"+std::string(family),key,std::to_string(f),std::to_string((f+1)%6),post);
            result.push_back(c);
            if(family=="piston") { c.ghost.name="minecraft:sticky_piston";c.predicted.name=c.ghost.name;result.push_back(c); }
        }
    } else if(family=="torch") {
        for(auto name:{"minecraft:torch","minecraft:redstone_torch","minecraft:unlit_redstone_torch"}) for(auto facing:{"top","north","south","west","east"}) {
            auto c=makeFamilyCase(name,"torch_facing_direction",facing,"unknown");
            if(c.ghost.name=="minecraft:unlit_redstone_torch") c.predicted.name="minecraft:redstone_torch";
            result.push_back(c);
        }
    } else if(family=="trapdoor"||family=="stairs") {
        for(int d=0;d<4;++d) for(int half=0;half<2;++half) {
            auto c=makeFamilyCase(family=="trapdoor"?"minecraft:oak_trapdoor":"minecraft:oak_stairs",
                family=="trapdoor"?"direction":"weirdo_direction",std::to_string(d),std::to_string((d+1)%4),
                family=="trapdoor"?"open_bit":"");
            c.ghost.states["upside_down_bit"]=std::to_string(half);c.predicted.states["upside_down_bit"]=std::to_string(half);
            result.push_back(c);
        }
    } else if(family=="slab") {
        for(auto key:{"minecraft:vertical_half","top_slot_bit"}) for(int half=0;half<2;++half) {
            auto value=std::string_view(key)=="top_slot_bit"?std::to_string(half):(half?"top":"bottom");
            auto wrong=std::string_view(key)=="top_slot_bit"?std::to_string(1-half):(half?"bottom":"top");
            result.push_back(makeFamilyCase("minecraft:stone_slab",key,value,wrong));
        }
    } else if(family=="door") {
        constexpr std::array<char const*,4> cardinal{{"north","east","south","west"}};
        for(auto key:{"direction","minecraft:cardinal_direction"}) for(int d=0;d<4;++d) for(int hinge=0;hinge<2;++hinge) {
            bool const legacy=std::string_view(key)=="direction";
            auto c=makeFamilyCase("minecraft:oak_door",key,legacy?std::to_string(d):cardinal[d],legacy?std::to_string((d+1)%4):cardinal[(d+1)%4],"open_bit");
            c.ghost.states["upper_block_bit"]="0";c.predicted.states["upper_block_bit"]="0";
            c.predicted.states["door_hinge_bit"]=std::to_string(hinge);
            c.upper=c.ghost;c.upper->states["upper_block_bit"]="1";c.upper->states["door_hinge_bit"]=std::to_string(hinge);
            result.push_back(c);
        }
    } else {
        constexpr std::array<char const*,4> cardinal{{"north","east","south","west"}};
        for(auto key:{"direction","minecraft:cardinal_direction"}) for(int d=0;d<4;++d) {
            bool const legacy=std::string_view(key)=="direction";
            auto c=makeFamilyCase("minecraft:unpowered_"+std::string(family),key,legacy?std::to_string(d):cardinal[d],legacy?std::to_string((d+1)%4):cardinal[(d+1)%4],family=="repeater"?"repeater_delay":"output_subtract_bit");
            result.push_back(c);c.ghost.name="minecraft:powered_"+std::string(family);result.push_back(c);
        }
    }
    return result;
}
inline void runCoverageChecks(Player& player) {
    auto& state=placementState();
    for(auto family:{"hopper","torch","trapdoor","stairs","slab","door","piston","observer","dispenser","dropper","repeater","comparator"}) {
        auto cases=familyCases(family);int stateCases=0;auto startChecks=checks,startFailures=failures;
        for(int mode=0;mode<3;++mode) {
            reset(player);if(mode==0) state.setEnabled(true);else if(mode==1) state.setManualMode(true);else state.setRangeEnabled(true);
            for(auto const& c:cases) {
                ++stateCases;
                auto upper=c.upper?&*c.upper:nullptr;
                check(placementPredictionMatches(c.predicted,c.ghost,upper),"family supplied state preserves controlled keys while permitting known post-placement difference");
                auto wrong=c.predicted;wrong.states[c.control]=c.wrong;
                check(!placementPredictionMatches(wrong,c.ghost,upper),"family rejects wrong placement-controlled direction/half");
                wrong=c.predicted;wrong.name="minecraft:unrelated_material";
                check(!placementPredictionMatches(wrong,c.ghost,upper),"family rejects a different material identity");
                if(c.ghost.states.contains("upside_down_bit")) {
                    wrong=c.predicted;wrong.states["upside_down_bit"]=wrong.states["upside_down_bit"]=="0"?"1":"0";
                    check(!placementPredictionMatches(wrong,c.ghost,upper),"stairs/trapdoor opposite half rejected in each mode");
                }
                if(c.upper) {
                    wrong=c.predicted;wrong.states["door_hinge_bit"]=wrong.states["door_hinge_bit"]=="0"?"1":"0";
                    check(!placementPredictionMatches(wrong,c.ghost,upper),"visible door hinge rejected in each mode");
                    wrong=c.predicted;wrong.states["upper_block_bit"]="1";
                    check(!placementPredictionMatches(wrong,c.ghost,upper),"upper/lower door bit rejected in each mode");
                }
            }
        }
        auto stateCheckCount=checks-startChecks,stateFailureCount=failures-startFailures;
        auto plannerStart=checks,plannerFailureStart=failures;int predictorBypasses=0;
        for(auto supportName:{"minecraft:chest","minecraft:hopper","minecraft:dropper","minecraft:dispenser","minecraft:unpowered_repeater","minecraft:unpowered_comparator"}) {
            for(int mode=0;mode<3;++mode) {
                reset(player);
                auto const& c=cases.front();Block support;support.name=supportName;support.mNetworkId=12;support.interactive=true;
                BlockPos cell{};uchar sf=0;if(auto deterministic=deterministicSupportDirection(c.ghost)) sf=*deterministic;
                player.region.cells[neighborOf(cell,sf)]=support;suppliedPrediction=&c.predicted;
                player.inventory.items[0]={1,0,64,101};useActualPlanner=true;
                aimed=ProjectionTarget{cell,cell,1,&c.ghost,{0.5f,0.25f,0.5f}};
                projection::candidates={{0,0,0,&c.ghost}};
                if(c.upper) projection::candidates.push_back({0,1,0,&*c.upper});
                if(mode==0) state.setEnabled(true);
                else if(mode==1) {
                    state.setManualMode(true);detail::fixtureTargetStatus=detail::ManualTargetStatus::Ready;
                    FixtureGameMode gm{player};gm.run(neighborOf(cell,sf),oppositeFace(sf),HandSlot::Mainhand);
                    check(gm.origins==0&&state.manualPlaceRequested(),"family manual hook queues ghost before supplied interactive origin");
                } else state.setRangeEnabled(true);
                tickEasyPlaceImpl(player);
                check(sent.size()==1,"family actual mode tick/planner/sender executes with supplied prediction and interactive classification");
                if(sent.size()==1) {
                    check(sent.back().pos==neighborOf(cell,sf)&&sent.back().face==oppositeFace(sf),"family mode transaction clicks supplied support with expected face");
                    check(sent.back().slot==0&&sent.back().item.netId==101,"family mode transaction preserves selected stack identity");
                }
                auto sends=sent.size();fixtureTime=1050;tickEasyPlaceImpl(player);
                check(sent.size()==sends,"family mode uses existing same-cell suppression before 500ms");
                if(predictionInputs.empty()) ++predictorBypasses;
            }
        }
        std::printf("COVERAGE {\"family\":\"%s\",\"state_cases\":%d,\"state_checks\":%d,\"state_failures\":%d,\"interactive_support_cases\":18,\"planner_checks\":%d,\"planner_failures\":%d,\"mode_pipeline_cases\":18,\"predictor_bypass_cases\":%d,\"native_prediction\":\"NOT_RUN\",\"native_interaction\":\"NOT_RUN\",\"server_apply_consumption\":\"NOT_RUN\",\"rotation_mirror\":\"NOT_RUN\"}\n",
            family,stateCases,stateCheckCount,stateFailureCount,checks-plannerStart,failures-plannerFailureStart,predictorBypasses);
    }
    // Special cases are kept separate from ordinary slab/door placement.
    reset(player);Block doubleSlab;doubleSlab.name="minecraft:double_stone_block_slab";doubleSlab.doubleSlab=true;
    check(placementPredictionMatches(doubleSlab,doubleSlab),"double slab exact runtime identity passes comparison fixture");
    Block single=doubleSlab;single.name="minecraft:stone_slab";single.doubleSlab=false;single.mNetworkId=2;
    check(!placementPredictionMatches(single,doubleSlab),"different single slab identity cannot satisfy double slab ghost");
    auto door=familyCases("door").front();suppliedPrediction=&door.predicted;Block support;player.region.cells[{0,-1,0}]=support;
    ProjectionTarget target;auto context=makePlacementContext({}, {}, 4);
    check(!resolveOrientedPlacementActual(player,player.region,context,{0,0,0},*door.upper,0,target),"upper door cell is never an independent planner target");
    player.region.cells[{0,1,0}]=support;
    check(!resolveOrientedPlacementActual(player,player.region,context,{0,0,0},door.ghost,0,target),"occupied upper door cell blocks lower door planner");
    check(block::placementItemName("minecraft:redstone_wall_torch")=="minecraft:redstone_torch","actual wall-redstone-torch inventory alias policy retained");
    reset(player);
}
