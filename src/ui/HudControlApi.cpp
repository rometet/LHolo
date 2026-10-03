#include "api/HudControlApi.h"
#include "structure/StructureUiState.h"
#include "app/HookLifecycle.h"
#ifndef LHOLO_HUD_CONTROL_TEST
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#else
extern bool lholoHudControlTestEnabled();
#endif
#include <cstdio>
namespace {
using State=lholo::structure::detail::StructureUiState;
bool enabled(){
#ifdef LHOLO_HUD_CONTROL_TEST
    return lholoHudControlTestEnabled();
#else
    return lholo::LHolo::getInstance().getSelf().isEnabled();
#endif
}
uint32_t __cdecl count() {
    lholo::app::hook_lifecycle::DetourGuard admission;
    if(!admission)return 0;
    try{return enabled()?uint32_t(lholo::input::kHotkeyCount):0;}catch(...){return 0;}
}
uint32_t __cdecl readHotkey(uint32_t id,HudControlHotkeyV1* out) {
    lholo::app::hook_lifecycle::DetourGuard admission;
    if(!admission)return 0;
    try {
        if(!enabled()||!out||out->size!=sizeof(*out)||id>=lholo::input::kHotkeyCount)return 0;
        auto v=State::getInstance().hotkey(id);
        // Slots 12..15 follow the parent's read-only directroute candidate
        // (OpenPlaced, OpenFiles, OpenVerification, OpenMaterials), all appended.
        constexpr char const* names[]{"Open menu","Move left","Move right","Move forward","Move backward","Move up","Move down","Layer increase","Layer decrease","Load projection","Close projection","Toggle manual placement","Open placed","Open files","Open verification","Open materials"};
        HudControlHotkeyV1 next{};next.size=sizeof(next);next.id=id;next.key=v.key;next.modifiers=v.modifiers;next.contexts=id==0||id>=12?3:1;
        if(id<sizeof(names)/sizeof(names[0]))std::snprintf(next.label,sizeof(next.label),"%s",names[id]);
        else {next.contexts=3;std::snprintf(next.label,sizeof(next.label),"Navigation action %u (metadata pending)",id);}
        *out=next;return 1;
    }catch(...){return 0;}
}
uint32_t __cdecl writeHotkey(uint32_t id,uint32_t key,uint32_t modifiers,uint32_t operation) {
    lholo::app::hook_lifecycle::DetourGuard admission;
    if(!admission)return 0;
    try {
        if(!enabled()||id>=lholo::input::kHotkeyCount||key>255||modifiers>7||operation>2||key==0x79||key==0x7A)return 0;
        auto& state=State::getInstance();state.stopHotkeyCapture();
        if(operation==0)state.setHotkey(id,key,modifiers);else if(operation==1)state.clearHotkey(id);else state.resetHotkey(id);
        state.resetHotkeyState();state.requestSettingsSave();return 1;
    }catch(...){return 0;}
}
uint32_t __cdecl readLayout(uint32_t id,HudControlLayoutV1* out){
    lholo::app::hook_lifecycle::DetourGuard admission;
    if(!admission)return 0;
    try{if(!enabled()||id>1||!out||out->size!=sizeof(*out))return 0;
        auto v=State::getInstance().hudLayout(id);*out={sizeof(*out),id,uint32_t(v.custom),uint32_t(v.anchor),v.x,v.y,v.scale};return 1;
    }catch(...){return 0;}
}
uint32_t __cdecl writeLayout(HudControlLayoutV1 const* value,uint32_t operation){
    lholo::app::hook_lifecycle::DetourGuard admission;
    if(!admission)return 0;
    try {if(!enabled()||!value||value->size!=sizeof(*value)||value->id>1||value->custom>1||operation>1)return 0;
        lholo::ui::HudLayout v{value->custom!=0,value->x,value->y,value->scale,int(value->anchor)};
        if(operation==1)v={};if(!lholo::ui::validHudLayout(v))return 0;
        auto& state=State::getInstance();state.setHudLayout(value->id,v);state.requestSettingsSave();return 1;
    }catch(...){return 0;}
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl lholo_get_hud_control_api_v1(uint32_t version,HudControlApiV1* out) noexcept {
    lholo::app::hook_lifecycle::DetourGuard admission;
    if(!admission)return 0;
    try{if(!enabled()||version!=1||!out||out->size!=sizeof(*out))return 0;
        *out={sizeof(*out),1,count,readHotkey,writeHotkey,readLayout,writeLayout};return 1;
    }catch(...){return 0;}
}
