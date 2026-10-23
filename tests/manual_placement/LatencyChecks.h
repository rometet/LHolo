#pragma once
// R14 latency/regression cases run against extracted production Easy Place
// bodies and explicit native boundary doubles. Not a server acceptance test.
inline void runLatencyChecks(Player& player) {
    auto& state=placementState();
    Block stone;
    Block piston;
    piston.name="minecraft:piston";
    piston.states={{"facing_direction","3"}};
    piston.mNetworkId=24;
    piston.material=2;
    int const before=checks;

    reset(player);
    player.selected=7;
    player.inventory.items[2]={1,0,64};
    player.inventory.items[7]={1,0,64};
    auto const direct=findItemSlot(player,stone);
    auto const preferred=findItemSlot(
        snapshotInventory(player),stone,ItemFind{7,&player.inventory.items[7]}
    );
    check(direct.slot==7 && preferred.slot==7,
        "R14 held material wins over earlier duplicate hotbar slot");
    check(findItemSlot(player,piston).slot<0,
        "R14 held priority never invents a different item id");

    // Range mode's priority must send the directly aimed ghost before
    // exploring nearby impossible direction candidates; no extra slot change.
    reset(player);state.setRangeEnabled(true);
    player.selected=7;
    player.inventory.items[2]={2,0,64};
    player.inventory.items[7]={1,0,64};
    projection::candidates={{0,0,0,&piston},{1,0,0,&piston},
        {2,0,0,&piston},{-1,0,0,&piston},{-2,0,0,&piston},
        {3,0,0,&stone}};
    aimed=ProjectionTarget{{3,0,0},{3,0,0},1,&stone,{3.5f,.25f,.5f}};
    planOutcomes[3]=true;
    tickEasyPlaceImpl(player);
    check(sent.size()==1 && sent[0].pos.x==3
        && sent[0].slot==7 && player.selected==7 && player.setSlotCalls==0,
        "R14 aimed and already-held stone sends in the first game tick");
    check(plannedCells.size()==1 && plannedCells.front()==3,
        "R14 crosshair-first avoids five costly direction candidates");
    check(projection::rangeQueries==0,
        "R15 focused block dispatch skips full radius query/sort");

    reset(player);state.setRangeEnabled(true);
    player.selected=7;
    player.inventory.items[7]={1,0,64};
    projection::candidates={{1,0,0,&stone}};
    aimed=ProjectionTarget{{3,0,0},{3,0,0},1,&stone,{3.5f,.25f,.5f}};
    planOutcomes[3]=false;
    planOutcomes[1]=true;
    tickEasyPlaceImpl(player);
    check(projection::rangeQueries==1 && sent.size()==1 && sent[0].pos.x==1,
        "R15 failed focused plan still falls back to range candidates");

    // R16: repeatedly failing an expensive oriented ghost should not force
    // the native predictor/rotation matrix to run every game tick. New aimed
    // cells remain immediately eligible even during the 100ms failed cache.
    reset(player);state.setEnabled(true);
    player.selected=0;
    player.inventory.items[0]={2,0,64};
    aimed=ProjectionTarget{{0,0,0},{0,0,0},1,&piston,{.5f,.25f,.5f}};
    planOutcomes[0]=false;
    tickEasyPlaceImpl(player);
    check(planned==1 && sent.empty(),
        "R16 first impossible aimed orientation is inspected");
    fixtureTime=1050;tickEasyPlaceImpl(player);
    check(planned==1 && sent.empty(),
        "R16 unchanged impossible orientation skips heavy repeated prediction");
    aimed=ProjectionTarget{{1,0,0},{1,0,0},1,&piston,{1.5f,.25f,.5f}};
    planOutcomes[1]=true;
    tickEasyPlaceImpl(player);
    check(planned==2 && sent.size()==1 && sent.back().pos.x==1,
        "R16 changed aimed ghost bypasses previous failed-plan cache");
    reset(player);state.setEnabled(true);
    player.selected=0;player.inventory.items[0]={2,0,64};
    aimed=ProjectionTarget{{0,0,0},{0,0,0},1,&piston,{.5f,.25f,.5f}};
    planOutcomes[0]=false;
    tickEasyPlaceImpl(player);
    fixtureTime=1100;tickEasyPlaceImpl(player);
    check(planned==2,
        "R16 retry becomes eligible again after bounded 100ms");

    // R17: connectivity flags on iron bars/panes/fences are environment-owned
    // and can differ between an initial native prediction and the structure.
    // Auto/Range used to reject this forever because the fallback was Manual-
    // only. These use the actual placementPredictionMatches body, not a mock.
    for(auto name:{"minecraft:iron_bars","minecraft:glass_pane",
        "minecraft:red_stained_glass_pane","minecraft:oak_fence",
        "minecraft:copper_bars","minecraft:stone_brick_wall"}) {
        reset(player);state.setEnabled(true);
        Block ghost{.name=name,
            .states={{"minecraft:connection_north","1"},
                     {"minecraft:connection_east","0"}},
            .mNetworkId=700,.material=7};
        Block predicted=ghost;
        predicted.mNetworkId=701;
        predicted.states["minecraft:connection_north"]="0";
        predicted.states["minecraft:connection_east"]="1";
        check(placementPredictionMatches(predicted,ghost),
            "R17 native predicted connection permutations accepted in Auto");
        predicted.states["unexpected_state"]="1";
        check(!placementPredictionMatches(predicted,ghost),
            "R17 unknown serialized state remains strict");
        predicted.states.erase("unexpected_state");
        player.inventory.items[0]={7,0,64};
        useActualPlanner=true;
        suppliedPrediction=&predicted;
        player.region.cells[{0,-1,0}] = Block{};
        ProjectionTarget output;
        PlacementContext ctx{{0.5f,0.5f,0.5f},64.0f};
        check(resolveOrientedPlacementActual(
            player,player.region,ctx,{0,0,0},ghost,0,output),
            "R17 native extractor planner accepts safe connectivity state");
        check(output.block==&ghost,
            "R17 native predictor keeps correct ghost block identity");
    }
    reset(player);state.setEnabled(true);
    Block differentColor{.name="minecraft:red_stained_glass_pane",
        .states={{"minecraft:connection_east","1"}},.mNetworkId=800};
    Block otherColor=differentColor;
    otherColor.name="minecraft:blue_stained_glass_pane";
    otherColor.mNetworkId=801;
    check(!placementPredictionMatches(otherColor,differentColor),
        "R17 stained pane color identity remains strict");
    Block customBar{.name="addon:iron_bars",
        .states={{"minecraft:connection_north","1"}},.mNetworkId=802};
    Block customBarPred=customBar;
    customBarPred.states["minecraft:connection_north"]="0";
    customBarPred.mNetworkId=803;
    check(!placementPredictionMatches(customBarPred,customBar),
        "R17 arbitrary addon blocks cannot bypass strict permutations");
    Block pistonDirection{.name="minecraft:piston",
        .states={{"facing_direction","3"}},.mNetworkId=804};
    Block wrongPiston=pistonDirection;
    wrongPiston.states["facing_direction"]="2";
    wrongPiston.mNetworkId=805;
    check(!placementPredictionMatches(wrongPiston,pistonDirection),
        "R17 piston facing remains strictly validated");

    // A queued native rotation is NOT a failed plan and must stop this
    // tick. Otherwise a later legacy placement changes the held slot
    // and the queued AuthInput fails its strict stack/net-id validation.
    reset(player);state.setRangeEnabled(true);
    player.selected=0;
    player.inventory.items[0]={2,0,64};
    player.inventory.items[1]={1,0,64};
    projection::candidates={{0,0,0,&piston},{1,0,0,&stone}};
    planOutcomes[0]=true;
    planOutcomes[1]=true;
    queueRotationOnPlan=true;
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(detail::rotatedQueueCalls==1 && sent.empty()
        && player.selected==0 && plannedCells.size()==1,
        "R14 queued native input prevents a competing legacy send");
    check(!state.recentPlacementActive(packBlockPos({0,0,0}),fixtureTime),
        "R14 queued use is not falsely acknowledged or cell-locked");

    // Manual mode responds on press, then repeats at the 50ms tick floor
    // after a short 100ms initial typematic delay. The existing same-cell
    // 500ms server-ack backoff remains in force.
    reset(player);state.setManualMode(true);
    player.selected=4;
    player.inventory.items[4]={1,0,64};
    aimed=ProjectionTarget{{0,0,0},{0,0,0},1,&stone,{.5f,.25f,.5f}};
    planOutcomes[0]=true;
    check(state.beginManualPress(1000,state.manualInputEpoch()),
        "R14 manual first press admitted");
    tickEasyPlaceImpl(player);
    check(sent.size()==1&&sent[0].time==1000&&sent[0].slot==4 && player.setSlotCalls==0,
        "R14 manual first placement still has no artificial delay");
    aimed=ProjectionTarget{{1,0,0},{1,0,0},1,&stone,{1.5f,.25f,.5f}};
    planOutcomes[1]=true;
    fixtureTime=1099;tickEasyPlaceImpl(player);
    check(sent.size()==1,
        "R14 manual initial repeat does not violate 100ms safety gap");
    fixtureTime=1100;tickEasyPlaceImpl(player);
    check(sent.size()==2&&sent.back().time==1100,
        "R14 first manual held repeat occurs at 100ms");
    aimed=ProjectionTarget{{2,0,0},{2,0,0},1,&stone,{2.5f,.25f,.5f}};
    planOutcomes[2]=true;
    fixtureTime=1149;tickEasyPlaceImpl(player);
    check(sent.size()==2,
        "R14 manual repeats cannot send before 50ms interval");
    fixtureTime=1150;tickEasyPlaceImpl(player);
    check(sent.size()==3&&sent.back().time==1150,
        "R14 subsequent manual repeat occurs at 50ms");

    // Negative prediction cache must not defer a newly reachable ordinary
    // block for a quarter-second. It still bounds CPU retry work.
    reset(player);state.setRangeEnabled(true);
    player.inventory.items[0]={1,0,64};
    projection::candidates={{0,0,0,&stone}};
    planOutcomes[0]=false;
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(planned==1&&sent.empty(),"R14 first failed prediction cached");
    fixtureTime=1149;
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(planned==1,"R14 retry cache suppresses attempts before 150ms");
    fixtureTime=1150;
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(planned==2,"R14 refreshed prediction permitted at 150ms");

    std::printf("EASYPLACE_R14_LATENCY_CHECKS checks=%d newFailures=%d "
                "manualDelay=100ms manualRepeat=50ms failedRetry=150ms "
                "heldSelectedFirst=1 aimedRangePriority=1 queuedDoesNotConsumeAck=1 "
                "native_server=NOT_RUN\n",
        checks-before,failures);
}
