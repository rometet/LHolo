#pragma once
// Compile the production native planner and dispatch bodies. This is NOT a
// server-authoritative acceptance test; input tick checks are separate.
inline void runStairStrictChecks(Player& player) {
    int const prior=failures;
    int accepted{},refused{};
    for(int actualDir=0;actualDir<4;++actualDir)
      for(int wantDir=0;wantDir<4;++wantDir)
        for(int half=0;half<2;++half) {
            reset(player);
            auto& state=placementState();
            state.setEnabled(true);
            orientationProbe=true;
            player.rotation={17.F,actualDir==3?-90.F:actualDir*90.F};
            Vec2 const original=player.rotation;
            Block support;support.mNetworkId=12;
            player.region.cells[{-1,0,0}]=support;
            Block stairs;stairs.name="minecraft:oak_stairs";
            stairs.states={{"weirdo_direction",std::to_string(wantDir)},
                {"upside_down_bit",std::to_string(half)}};
            stairs.mNetworkId=20;stairs.material=1;
            ProjectionTarget plan;
            bool const ok=resolveOrientedPlacementActual(
                player,player.region,makePlacementContext({}, {}, 4),
                {},stairs,0,plan);
            check(player.rotation==original,
                "R18 stairs never temporarily spoof player rotation");
            if(actualDir!=wantDir) ++refused; else ++accepted;
            check(ok,"R19 stair prediction accepts every yaw with matching native probe");
            if(ok) {
                    check(plan.requireNativeRotationSync
                        && plan.interactionRotation.has_value()
                        && plan.at==BlockPos{-1,0,0},
                        "R18 stair requires native sync and non-air support");
                    auto const queued=placeBlock(player,plan,0,{1,0,64,101});
                    check(queued==detail::PlacementDispatchResult::Queued
                        && detail::rotatedQueueStrict && sent.empty(),
                        "R18 correct stair is queued, not legacy-sent");
                    check(detail::rotatedQueueYaw==(wantDir==3?-90.F:wantDir*90.F),
                        "R19 yaw override matches exact native predictor direction");
            }
        }
    reset(player);placementState().setEnabled(true);
    orientationProbe=true;player.rotation={17.F,90.F};
    Block stairs;stairs.name="minecraft:oak_stairs";stairs.mNetworkId=20;
    stairs.states={{"weirdo_direction","1"},{"upside_down_bit","0"}};
    ProjectionTarget plan;
    check(!resolveOrientedPlacementActual(player,player.region,
        makePlacementContext({}, {}, 4),{},stairs,0,plan),
        "R18 floating stair cannot use synthetic air-click placement");

    reset(player);placementState().setEnabled(true);
    orientationProbe=true;player.rotation={17.F,90.F};
    Block support;support.mNetworkId=12;
    player.region.cells[{-1,0,0}]=support;
    stairs.material=1;
    player.inventory.items[1]={1,0,64,101};
    player.selected=0;
    aimed=ProjectionTarget{{0,0,0},{-1,0,0},5,&stairs,{0,.25f,.5f}};
    useActualPlanner=true;
    tickEasyPlaceImpl(player);
    check(player.selected==1 && detail::rotatedQueueCalls==0
        && sent.empty(),
        "R18 stair waits a tick after hotbar selection to avoid server race");
    fixtureTime+=50;
    tickEasyPlaceImpl(player);
    check(detail::rotatedQueueCalls==1 && detail::rotatedQueueStrict
        && sent.empty(),
        "R18 stair queues only after hotbar already selected");

    std::printf("STAIRS_R18_STRICT checks=%d accepted=%d refused=%d failures=%d "
        "native_server=NOT_RUN\n",checks,accepted,refused,failures-prior);
}
