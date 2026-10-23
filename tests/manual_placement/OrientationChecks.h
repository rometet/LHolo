#pragma once
inline void runOrientationChecks(Player& player) {
    int admitted=0;int const previousFailures=failures;
    for(int playerDirection=0;playerDirection<4;++playerDirection) for(int ghostDirection=0;ghostDirection<4;++ghostDirection) for(int half=0;half<2;++half) {
        reset(player);placementState().setEnabled(true);orientationProbe=true;player.rotation={17.0f,playerDirection==3?-90.0f:playerDirection*90.0f};
        Vec2 const original=player.rotation;
        Block trapdoor;trapdoor.name="minecraft:oak_trapdoor";trapdoor.mNetworkId=20;
        trapdoor.states={{"direction",std::to_string(ghostDirection)},{"upside_down_bit",std::to_string(half)},{"open_bit","1"}};
        Block support;support.mNetworkId=12;player.region.cells[{-1,0,0}]=support;
        ProjectionTarget target;
        bool const plannedOk=resolveOrientedPlacementActual(player,player.region,makePlacementContext({}, {}, 4),{},trapdoor,0,target);
        check(plannedOk,"trapdoor 4 player x 4 ghost x 2 half admits native-boundary direction independently of player yaw");
        check(player.rotation==original,"trapdoor prediction restores actor pitch/yaw before returning");
        if(plannedOk) ++admitted;
        check(!plannedOk || target.at==BlockPos{-1,0,0},"trapdoor chooses real support for native click");
        check(!plannedOk || (target.clickPos.y>=0.5f)==(half==1),"trapdoor hit half agrees with supplied native prediction");
        if(plannedOk) {
            auto const sentOk=placeBlock(player,target,0,{1,0,64,101});
            if(playerDirection==ghostDirection) check(detail::isPlacementSent(sentOk)&&sent.size()==1,"aligned trapdoor retains existing sender path");
            else check(sentOk==detail::PlacementDispatchResult::Queued&&sent.empty()&&detail::rotatedQueueCalls==1&&detail::rotatedQueueYaw==(ghostDirection==3?-90.0f:ghostDirection*90.0f),
                "off-axis trapdoor queues interaction yaw without declaring sender/ack success");
        }
    }
    std::printf("ROTATION_MATRIX {\"cases\":32,\"admitted\":%d,\"failures\":%d,\"prediction\":\"SUPPLIED_YAW_HALF_MODEL\",\"native_server\":\"NOT_RUN\"}\n",admitted,failures-previousFailures);
    reset(player);orientationProbe=true;throwOnOverride=true;player.rotation={17,90};
    Block trapdoor;trapdoor.name="minecraft:oak_trapdoor";trapdoor.states={{"direction","0"},{"upside_down_bit","0"}};
    Block support;player.region.cells[{-1,0,0}]=support;ProjectionTarget target;
    check(!resolveOrientedPlacementActual(player,player.region,makePlacementContext({}, {}, 4),{},trapdoor,0,target),"supplied prediction fault rejects override");
    check(player.rotation==Vec2{17,90},"fault boundary restores actor logic rotation");
    reset(player);orientationProbe=true;player.rotation={17,90};trapdoor.name="custom:oak_trapdoor";
    player.region.cells[{-1,0,0}]=support;
    check(!resolveOrientedPlacementActual(player,player.region,makePlacementContext({}, {}, 4),{},trapdoor,0,target),"rotation override is limited to known vanilla namespace");
    // R13 directional matrix: the native prediction model is supplied by the
    // harness, while the production planner and interaction-rotation send path
    // are compiled from the unchanged source bodies.
    int sixAxisAdmitted{};
    for (auto const type : {"minecraft:observer","minecraft:piston",
            "minecraft:sticky_piston","minecraft:dispenser",
            "minecraft:dropper","minecraft:barrel"}) {
        for(int direction=0;direction<6;++direction) {
            reset(player);placementState().setEnabled(true);
            orientationProbe=true;player.rotation={17.F,90.F};
            auto const original=player.rotation;
            Block solid;solid.mNetworkId=12;
            player.region.cells[{-1,0,0}]=solid;
            Block ghost;ghost.name=type;ghost.mNetworkId=20;
            ghost.states={{"facing_direction",std::to_string(direction)}};
            ProjectionTarget proposed;
            bool const matched=resolveOrientedPlacementActual(
                player,player.region,makePlacementContext({}, {}, 4),{},ghost,0,proposed);
            check(matched,"six-way block predicts requested direction via native-boundary rotation matrix");
            check(player.rotation==original,"scoped pitch and yaw restored after six-way planning");
            if(!matched)continue;
            ++sixAxisAdmitted;
            auto const queuedBefore=detail::rotatedQueueCalls;
            auto const wasSent=placeBlock(player,proposed,0,{1,0,64,101});
            if(direction==4) {
                check(detail::isPlacementSent(wasSent)&&sent.size()==1&&queuedBefore==detail::rotatedQueueCalls,
                    "already-aligned six-way placement retains unchanged native sender path");
            } else {
                check(wasSent==detail::PlacementDispatchResult::Queued&&sent.empty()&&detail::rotatedQueueCalls==1,
                    "off-axis six-way placement queues one native interaction, no fake ack");
                float const expectedPitch=direction==1?-80.F:direction==0?80.F:0.F;
                check(detail::rotatedQueuePitch==expectedPitch,
                    "vertical six-way family carries the exact selected native-predictor pitch");
                if(direction>=2) {
                    float const expectedYaw=direction==3?0.F:direction==4?90.F:
                        direction==2?180.F:-90.F;
                    check(detail::rotatedQueueYaw==expectedYaw,
                        "horizontal six-way family queues the matched yaw");
                }
            }
        }
    }
    std::printf("SIX_AXIS_PLANNER_MATRIX {\"cases\":36,\"admitted\":%d,\"native_server\":\"NOT_RUN\"}\n",sixAxisAdmitted);
    int crafterAdmitted{};
    for(auto axis : {"horizontal","up","down"}) for(int heading=0;heading<4;++heading) {
        reset(player);placementState().setEnabled(true);orientationProbe=true;
        player.rotation={17.F,90.F};auto const saved=player.rotation;
        Block solid;solid.mNetworkId=12;player.region.cells[{-1,0,0}]=solid;
        Block ghost;ghost.name="minecraft:crafter";ghost.mNetworkId=20;
        ghost.states={{"orientation",std::string(axis)+"_"+std::to_string(heading)}};
        ProjectionTarget target;
        bool const ok=resolveOrientedPlacementActual(player,player.region,
            makePlacementContext({}, {}, 4),{},ghost,0,target);
        check(ok,"crafter 3D matrix predicts target combination");
        check(player.rotation==saved,"crafter restores native actor mRot even after pitch/yaw probing");
        if(!ok)continue;
        ++crafterAdmitted;
        auto const sentOk=placeBlock(player,target,0,{1,0,64,101});
        if(std::string_view(axis)=="horizontal"&&heading==1) {
            check(detail::isPlacementSent(sentOk)&&sent.size()==1,"aligned crafter uses original sender");
        } else {
            check(sentOk==detail::PlacementDispatchResult::Queued&&sent.empty()&&detail::rotatedQueueCalls==1,
                "3D crafter queues matched pitch/yaw only");
            float const pitch=std::string_view(axis)=="up"?-80.F:
                std::string_view(axis)=="down"?80.F:0.F;
            check(detail::rotatedQueuePitch==pitch,"crafter selected pitch matches bounded matrix");
        }
    }
    std::printf("CRAFTER_3D_MATRIX {\"cases\":12,\"admitted\":%d,\"native_server\":\"NOT_RUN\"}\n",crafterAdmitted);
    reset(player);orientationProbe=true;throwOnOverride=true;player.rotation={17.F,90.F};
    Block solid;solid.mNetworkId=12;player.region.cells[{-1,0,0}]=solid;
    Block upward;upward.name="minecraft:observer";upward.mNetworkId=20;
    upward.states={{"facing_direction","1"}};
    ProjectionTarget failed;
    check(!resolveOrientedPlacementActual(player,player.region,
        makePlacementContext({}, {}, 4),{},upward,0,failed),
        "native vertical prediction exception rejects rather than sending blind rotation");
    check(player.rotation==Vec2{17.F,90.F},
        "native prediction exception restores both pitch and yaw");
    reset(player);
}
