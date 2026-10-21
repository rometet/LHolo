#include "api/HudControlApi.h"
#include "app/HookLifecycle.h"
#include "structure/StructureUiState.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <limits>
#include <thread>

extern "C" uint32_t __cdecl lholo_get_hud_control_api_v1(uint32_t,HudControlApiV1*) noexcept;
static_assert(sizeof(HudControlHotkeyV1)==148 && alignof(HudControlHotkeyV1)==4);
static_assert(sizeof(HudControlLayoutV1)==28 && alignof(HudControlLayoutV1)==4);
static_assert(sizeof(HudControlApiV1)==48 && alignof(HudControlApiV1)==8);
static_assert(offsetof(HudControlHotkeyV1,label)==20);
static_assert(offsetof(HudControlApiV1,hotkey_count)==8 && offsetof(HudControlApiV1,write_layout)==40);

namespace {
std::atomic_bool sdkEnabled{true},blockLookup{},enteredLookup{},releaseLookup{};
std::atomic_uint enabledReads{};
}
// Only the SDK's loader-enabled query is substituted. All exported provider
// functions, lifecycle admission, bindings, state and save requests are real.
bool lholoHudControlTestEnabled() {
    ++enabledReads;
    if(blockLookup) {
        enteredLookup=true;
        enteredLookup.notify_all();
        while(!releaseLookup)releaseLookup.wait(false);
    }
    return sdkEnabled;
}
int main() {
    using namespace lholo::app::hook_lifecycle;
    auto& state=lholo::structure::detail::StructureUiState::getInstance();
    unsigned checks{},failures{};
    auto check=[&](bool ok,char const* label){++checks;if(!ok){++failures;std::cerr<<label<<'\n';}};
    HudControlApiV1 api{};api.size=sizeof(api);
    check(!lholo_get_hud_control_api_v1(1,&api)&&enabledReads==0,"disabled rejects before singleton lookup");
    check(beginEnable(),"enter running");
    check(!lholo_get_hud_control_api_v1(2,&api),"wrong ABI rejected");
    check(lholo_get_hud_control_api_v1(1,&api)&&api.version==1,"acquire actual API");
    check(api.hotkey_count()==16,"all sixteen persistent slots exposed");
    for(unsigned id=0;id<16;++id) {
        HudControlHotkeyV1 value{};value.size=sizeof(value);
        check(api.read_hotkey(id,&value)&&value.id==id&&value.label[0],"slot metadata");
        check(value.contexts==(id==0||id>=12?3u:1u),"slot contexts");
        (void)state.consumePendingHotkeyActions();
        check(api.write_hotkey(id,'Q',3,0),"set existing slot");
        check(state.hotkey(id).key=='Q'&&state.hotkey(id).modifiers==3,"native modifiers retained");
        check(state.consumePendingHotkeyActions().settingsSave,"set requests persistence");
        check(api.write_hotkey(id,0,0,1)&&state.hotkey(id).key==0,"clear");
        check(api.write_hotkey(id,0,0,2),"reset");
    }
    check(!api.write_hotkey(16,'Q',0,0)&&!api.write_hotkey(0,256,0,0)
        &&!api.write_hotkey(0,'Q',8,0)&&!api.write_hotkey(0,'Q',0,3),"reject malformed bindings");
    check(!api.write_hotkey(0,0x79,0,0)&&!api.write_hotkey(0,0x7A,0,0),"F10/F11 preserved");
    HudControlHotkeyV1 bad{};bad.size=1;
    check(!api.read_hotkey(0,&bad)&&!api.read_hotkey(0,nullptr),"malformed read POD rejected");
    for(unsigned id=0;id<2;++id) {
        HudControlLayoutV1 layout{sizeof(layout),id,1,8,25,-12,.75f};
        check(api.write_layout(&layout,0),"set layout");
        HudControlLayoutV1 read{};read.size=sizeof(read);
        check(api.read_layout(id,&read)&&read.custom&&read.x==25&&read.scale==.75f,"read layout");
        layout.scale=std::numeric_limits<float>::infinity();
        check(!api.write_layout(&layout,0),"nonfinite layout rejected");
        layout.scale=.75f;check(api.write_layout(&layout,1),"reset layout");
        check(api.read_layout(id,&read)&&!read.custom,"reset restores legacy layout");
    }
    sdkEnabled=false;check(api.hotkey_count()==0&&!api.write_hotkey(0,'Z',0,0),"loader-disabled rejection");
    sdkEnabled=true;(void)state.consumePendingHotkeyActions();
    blockLookup=true;
    auto admitted=std::async(std::launch::async,[&]{return api.write_hotkey(15,'V',4,0);});
    auto const deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!enteredLookup&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    check(enteredLookup,"provider operation admitted before quiesce");
    beginQuiesce();
    auto drain=std::async(std::launch::async,[]{waitForRunningCallbacks();});
    check(drain.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout,"teardown waits admitted provider");
    auto const reads=enabledReads.load();
    HudControlLayoutV1 layout{sizeof(layout),0,1,0,0,0,1};
    check(api.hotkey_count()==0&&!api.read_hotkey(0,&bad)&&!api.write_hotkey(0,'Z',0,0)
        &&!api.read_layout(0,&layout)&&!api.write_layout(&layout,0)
        &&!lholo_get_hud_control_api_v1(1,&api),"every API rejects quiescing");
    check(enabledReads==reads,"quiescing never accesses plugin singleton");
    releaseLookup=true;releaseLookup.notify_all();
    check(admitted.get()==1,"admitted write finishes consistently");
    check(drain.wait_for(std::chrono::seconds(5))==std::future_status::ready,"provider releases drain");
    drain.get();waitForQuiescence();markDisabled();
    check(state.hotkey(15).key=='V'&&state.consumePendingHotkeyActions().settingsSave,"final save observes admitted write");
    check(!api.write_hotkey(15,'Z',0,0)&&!state.consumePendingHotkeyActions().settingsSave,"no writes after final save boundary");
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
