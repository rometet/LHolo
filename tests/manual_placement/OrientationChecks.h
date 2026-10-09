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
            bool const sentOk=placeBlock(player,target,0,{1,0,64,101});
            if(playerDirection==ghostDirection) check(sentOk&&sent.size()==1,"aligned trapdoor retains existing sender path");
            else check(!sentOk&&sent.empty()&&detail::rotatedQueueCalls==1&&detail::rotatedQueueYaw==(ghostDirection==3?-90.0f:ghostDirection*90.0f),
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
    reset(player);
}
