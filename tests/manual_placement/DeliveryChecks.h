// Adversarial delivery models exercise actual sender/scheduler bodies. Policies
// below are explicit assumptions, not a reconstruction of Minecraft/BDS.
#pragma once

struct DeliveryModel {
    Player server;
    int acceptedPlaces{},rejectedPlaces{},consumed{},acceptedSwaps{},rejectedSwaps{};
    explicit DeliveryModel(Player const& local):server(local) {}
    bool swap(SwapSent const& packet,bool validateBefore=true) {
        if(packet.actions.size()!=2) { ++rejectedSwaps;return false; }
        if(validateBefore) for(auto const& a:packet.actions) {
            if(server.inventory.items.at(a.slot)!=a.before) { ++rejectedSwaps;return false; }
        }
        // Atomic compare/write is a MODEL POLICY, not inferred server behavior.
        for(auto const& a:packet.actions) server.inventory.items.at(a.slot)=a.after;
        ++acceptedSwaps;return true;
    }
    bool place(Sent const& packet,bool validateBlockId=true,bool validateStackCount=true) {
        auto& held=server.inventory.items.at(packet.slot);
        if(held.isNull()||held.key!=packet.item.key||held.aux!=packet.item.aux||held.netId!=packet.item.netId
            ||(validateStackCount&&held.count!=packet.item.count)) { ++rejectedPlaces;return false; }
        auto const& clicked=server.region.getBlock(packet.pos);
        if(validateBlockId&&clicked.mNetworkId!=packet.targetId) { ++rejectedPlaces;return false; }
        // Air-position vs support-neighbour interpretation is supplied here.
        // It is one of the native behaviors the runtime protocol must verify.
        auto const cell=clicked.isAir()?packet.pos:packet.pos+Facing::DIRECTION().at(packet.face);
        if(!server.region.getBlock(cell).isAir()) { ++rejectedPlaces;return false; }
        Block placed;placed.material=held.key;server.region.cells[cell]=placed;
        --held.count;++consumed;++acceptedPlaces;return true;
    }
    void reflectInventory(Player& local) const { local.inventory=server.inventory; }
    void reflectWorld(Player& local) const { local.region=server.region; }
};

inline void setupCrosshair(Player& player,Block const& block) {
    player.inventory.items[0]={block.material,0,64,101};planOutcomes[0]=true;
    aimed=ProjectionTarget{{0,0,0},{0,0,0},1,&block,{0.5f,0.25f,0.5f}};
    projection::candidates={{0,0,0,&block}};
}

inline void runDeliveryChecks(Player& player) {
    Block stone;
    auto& state=placementState();
    // Three modes, dropped/refused/unreflected results. The timer is not an ack.
    for(int mode=0;mode<3;++mode) {
        reset(player);setupCrosshair(player,stone);
        if(mode==0) state.setEnabled(true);
        else if(mode==1) { state.setManualMode(true);check(state.beginManualPress(1000,state.manualInputEpoch()),"held manual delivery scenario admitted"); }
        else state.setRangeEnabled(true);
        for(auto t:{1000ULL,1040ULL,1499ULL,1500ULL,1999ULL,2000ULL}) { fixtureTime=t;tickEasyPlaceImpl(player); }
        check(sent.size()==3,"unreflected cell emits at 500ms in Easy/held Manual/Range");
        check(sent[0].time==1000&&sent[1].time==1500&&sent[2].time==2000,"500ms same-cell floor retained independently of result delivery");
        DeliveryModel strict(player);
        check(strict.place(sent[2],true,false),"strict target-id model accepts latest request delivered first");
        check(!strict.place(sent[0],true,false)&&!strict.place(sent[1],true,false),"strict target-id model rejects reordered stale target snapshots");
        check(strict.acceptedPlaces==1&&strict.consumed==1,"single consumption is conditional on strict model validation");
        auto sends=sent.size();strict.reflectWorld(player);fixtureTime=2500;tickEasyPlaceImpl(player);
        check(sent.size()==sends,"reflected occupied cell prevents later client resend");
        std::printf("delivery mode=%d delayed/refused/drop: send_calls=3 lock=500ms strict-model applied=1 consumed=1 native=NOT_RUN\n",mode);
    }

    // Counterexample under a second, deliberately weaker server policy.
    // A successful test reports the absence of a CLIENT-ONLY guarantee.
    reset(player);setupCrosshair(player,stone);state.setEnabled(true);
    fixtureTime=1000;tickEasyPlaceImpl(player);fixtureTime=1500;tickEasyPlaceImpl(player);
    DeliveryModel permissive(player);
    check(permissive.place(sent[1],false,false)&&permissive.place(sent[0],false,false),"weaker model allows both same-intent delayed requests");
    check(permissive.consumed==2&&permissive.acceptedPlaces==2,"client-only at-most-once is not proven by 500ms timer");
    std::printf("delivery UNGUARANTEED: weaker-model repeated air-position packets consumed=2; NOT a BDS observation\n");

    // Quick tap has no held repeat; this is a stronger local-send property.
    reset(player);setupCrosshair(player,stone);state.setManualMode(true);
    check(state.beginManualPress(1000,state.manualInputEpoch()),"delivery quick tap admitted");state.releaseManualPress();
    for(auto t:{1000ULL,1500ULL,2000ULL,10000ULL}) { fixtureTime=t;tickEasyPlaceImpl(player); }
    check(sent.size()==1,"released manual quick tap sends once even with no reflected result");

    // Rejected/delayed swap: repeated packet snapshots at 200ms, no local swap.
    reset(player);player.inventory.items[20]={1,0,64,201};projection::candidates={{0,0,0,&stone}};planOutcomes[0]=true;
    DeliveryModel swapServer(player);
    for(auto t:{1000ULL,1050ULL,1199ULL,1200ULL,1400ULL}) { fixtureTime=t;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4)); }
    check(swapSent.size()==3&&sent.empty(),"unreflected swap retries at 200ms and never places from backpack");
    check(player.inventory.items[20].count==64&&player.inventory.items[0].isNull(),"swap send does not mutate local inventory");
    check(swapSent[0].time==1000&&swapSent[1].time==1200&&swapSent[2].time==1400,"actual swap sender retains 200ms floor");
    check(swapServer.swap(swapSent[2]),"strict swap model accepts reordered latest snapshot first");
    check(!swapServer.swap(swapSent[0])&&!swapServer.swap(swapSent[1]),"strict swap model rejects replayed before-images");
    check(swapServer.acceptedSwaps==1&&swapServer.rejectedSwaps==2,"one applied swap is conditional on before-image validation");
    swapServer.reflectInventory(player);fixtureTime=1450;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==1&&sent.back().slot==0,"later tick uses reflected hotbar item");
    check(swapServer.place(sent.back()),"strict delivery model accepts place after applied swap");
    check(swapServer.consumed==1,"model consumption remains one after validated swap");

    // Client hotbar visibility itself is not an acknowledgement. Supply a local
    // prediction before the modeled server has applied the swap.
    reset(player);player.inventory.items[20]={1,0,64,201};projection::candidates={{0,0,0,&stone}};planOutcomes[0]=true;
    DeliveryModel laggedServer(player);tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    player.inventory.items[0]=player.inventory.items[20];player.inventory.items[20]={};
    fixtureTime=1050;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==1&&!laggedServer.place(sent.back()),"predicted hotbar visibility can send before modeled server applies swap");
    check(laggedServer.consumed==0,"strict model rejects out-of-order place without consumption");
    check(laggedServer.swap(swapSent.front()),"delayed modeled swap eventually applies");
    laggedServer.reflectInventory(player);fixtureTime=1550;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==2&&laggedServer.place(sent.back()),"unreflected reject retries only after cell lock then can converge in model");
    std::printf("swap UNGUARANTEED: local-hotbar-next-tick is observation, not server-ack; out-of-order model rejected first place\n");

    // A full hotbar exchanges two populated slots. Preserve before/after images.
    reset(player);for(int i=0;i<9;++i) player.inventory.items[i]={2,0,32,300+i};
    player.selected=3;player.inventory.items[20]={1,0,64,201};projection::candidates={{0,0,0,&stone}};planOutcomes[0]=true;
    DeliveryModel fullBar(player);tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(swapSent.size()==1&&swapSent[0].actions.size()==2,"full-hotbar actual NormalTransaction contains two actions");
    auto const& actions=swapSent[0].actions;
    check(actions[0].slot==20&&actions[1].slot==3,"full hotbar swap targets selected slot only");
    check(actions[0].before.key==1&&actions[0].after.key==2&&actions[1].before.key==2&&actions[1].after.key==1,"actual swap payload preserves both item identities and counts");
    check(fullBar.swap(swapSent[0]),"strict full-hotbar model applies atomically");
    check(fullBar.server.inventory.items[20].key==2&&fullBar.server.inventory.items[20].count==32&&fullBar.server.inventory.items[3].count==64,"strict model preserves displaced hotbar stack");

    // A server-side inventory change refuses the before-image; no success is
    // inferred from the sender call or from a stale local stack.
    reset(player);player.inventory.items[20]={1,0,64,201};projection::candidates={{0,0,0,&stone}};planOutcomes[0]=true;
    DeliveryModel changedInventory(player);changedInventory.server.inventory.items[20]={3,0,10,401};
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(!changedInventory.swap(swapSent.front()),"model refuses swap after server inventory changed");
    fixtureTime=1200;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(swapSent.size()==2&&sent.empty(),"rejected swap has no local success marker and remains rate bounded");
    changedInventory.reflectInventory(player);fixtureTime=1400;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(swapSent.size()==2&&sent.empty(),"reflected missing material prevents subsequent swap/place");

    // Source-proven defect: factory explicitly returned null, so nothing was
    // sent. The 081a caller nevertheless ends the whole range-placement tick.
    reset(player);Block other=stone;other.material=2;other.mNetworkId=2;
    player.inventory.items[20]={1,0,64,201};player.inventory.items[0]={2,0,64,101};
    projection::candidates={{0,0,0,&stone},{1,0,0,&other}};planOutcomes[0]=true;planOutcomes[1]=true;normalFactoryAvailable=false;
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    std::printf("swap-factory-null normal_calls=%d swap_sends=%zu place_sends=%zu nextSwapAt=%llu\n",normalFactoryCalls,swapSent.size(),sent.size(),static_cast<unsigned long long>(state.nextSwapAt()));
    check(swapSent.empty()&&sent.size()==1&&sent.back().pos.x==1,"failed swap creation must not starve later valid hotbar candidate in same tick");
    check(state.nextSwapAt()==0,"unsent swap must not install server-swap backoff");

    for(bool manual:{false,true}) {
        reset(player);setupCrosshair(player,stone);
        player.inventory.items[20]=player.inventory.items[0];player.inventory.items[0]={};
        if(manual) {
            state.setManualMode(true);
            check(state.beginManualPress(1000,state.manualInputEpoch()),"manual swap recovery fixture press admitted");
        }
        else state.setEnabled(true);
        normalFactoryAvailable=false;tickEasyPlaceImpl(player);
        check(swapSent.empty()&&state.nextSwapAt()==0,"Easy/Manual unsent factory failure cannot become pending server swap");
        normalFactoryAvailable=true;fixtureTime=1050;tickEasyPlaceImpl(player);
        check(swapSent.size()==1&&sent.empty(),"Easy/Manual factory recovery sends swap on next eligible tick with no invented backoff");
    }

    // No sender/factory means no recent-cell success entry.
    reset(player);setupCrosshair(player,stone);factoryAvailable=false;
    tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.empty()&&!state.recentPlacementActive(packBlockPos({}),1000),"failed ItemUse creation is not marked sent/applied");
    factoryAvailable=true;fixtureTime=1250;tickRangePlaceImpl(player,makePlacementContext({}, {}, 4));
    check(sent.size()==1,"failed-cache expiry allows factory recovery");

    // Actual manual start-build decision body; native block classification and
    // query outcomes are supplied. This verifies precedence, not engine use.
    for(auto name:{"minecraft:chest","minecraft:hopper","minecraft:dropper","minecraft:dispenser","minecraft:unpowered_repeater","minecraft:unpowered_comparator"}) {
        for(auto status:{detail::ManualTargetStatus::Ready,detail::ManualTargetStatus::MissingMaterial,detail::ManualTargetStatus::None}) {
            reset(player);state.setManualMode(true);Block support;support.name=name;support.interactive=true;player.region.cells[{1,0,0}]=support;
            detail::fixtureTargetStatus=status;FixtureGameMode gm{player};gm.run({1,0,0},4,HandSlot::Mainhand);
            if(status==detail::ManualTargetStatus::Ready) {
                check(gm.origins==0&&state.manualPlaceRequested(),"ready ghost takes precedence over supplied interactive support in actual manual hook body");
            } else {
                check(gm.origins==1&&!state.manualPlaceRequested()&&structure::hints.empty(),"without ready ghost supplied interactive support retains native origin");
            }
        }
    }
    reset(player);state.setManualMode(true);detail::fixtureTargetStatus=detail::ManualTargetStatus::MissingMaterial;
    FixtureGameMode missing{player};missing.run({1,0,0},4,HandSlot::Mainhand);
    check(missing.origins==0&&structure::hints.size()==1&&structure::hints.front()==i18n::TextKey::ActionHintNoMatchingItem,"noninteractive missing material remains one-shot hint and blocks native placement");
    reset(player);state.setManualMode(true);heldExempt=true;detail::fixtureTargetStatus=detail::ManualTargetStatus::Ready;
    FixtureGameMode exempt{player};exempt.run({1,0,0},4,HandSlot::Mainhand);
    check(exempt.origins==1&&!state.manualPlaceRequested(),"exempt held item still reaches vanilla origin near ghost");
    reset(player);
}
