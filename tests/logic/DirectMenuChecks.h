#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "input/MenuRoute.h"
#include "input/NativeTextInputState.h"
#include "structure/StructureUiState.h"
#include "ui/MenuRoutePresentation.h"

namespace lholo::tests {
template<class Check> void runDirectMenuChecks(Check check) {
    using namespace input;
    check(static_cast<std::size_t>(HotkeyId::OpenPlaced)==12);
    check(kHotkeyCount==16 && kDirectMenuHotkeyCount==4);
    check(menuRouteForHotkey(11)==MenuRoute::None);
    check(menuRouteForHotkey(12)==MenuRoute::Placed);
    check(menuRouteForHotkey(13)==MenuRoute::Files);
    check(menuRouteForHotkey(14)==MenuRoute::Verification);
    check(menuRouteForHotkey(15)==MenuRoute::Materials);
    check(menuRouteForHotkey(16)==MenuRoute::None);
    check(directMenuForegroundMatches(17,17));
    check(!directMenuForegroundMatches(17,18));
    check(!directMenuForegroundMatches(0,0));
    DirectMenuInputContext context{true,false,false,true,false,false};
    check(directMenuInputAllowed(context));
    context.nativeTextInputBlocked=true;check(!directMenuInputAllowed(context));
    context.nativeTextInputBlocked=false;context.uiInteractionBlocked=true;check(!directMenuInputAllowed(context));
    context.uiInteractionBlocked=false;context.companionVisible=true;check(!directMenuInputAllowed(context));
    context.companionVisible=false;context.foreground=false;check(!directMenuInputAllowed(context));
    context.foreground=true;context.gameplayInputEnabled=false;check(!directMenuInputAllowed(context));
    context.lholoVisible=true;check(directMenuInputAllowed(context));

    NativeTextInputState native;
    for(auto flag:{NativeTextInputFlag::Focus,NativeTextInputFlag::Keyboard,NativeTextInputFlag::Ime,NativeTextInputFlag::GameplayDisabled}) {
        native.gain(flag);auto old=native.token(flag);native.gain(flag);
        check(!native.clearIfCurrent(flag,old));check(native.blocked());
        check(native.clearIfCurrent(flag,native.token(flag)));check(!native.blocked());
        native.gain(flag);old=native.token(flag);native.reset();
        check(!native.clearIfCurrent(flag,old));check(!native.blocked());
    }
    native.gain(NativeTextInputFlag::Focus);native.gain(NativeTextInputFlag::Keyboard);
    check(native.clearIfCurrent(NativeTextInputFlag::Focus,native.token(NativeTextInputFlag::Focus)));
    check(native.blocked());
    check(native.clearIfCurrent(NativeTextInputFlag::Keyboard,native.token(NativeTextInputFlag::Keyboard)));
    check(!native.blocked());

    auto& state=structure::detail::StructureUiState::getInstance();
    state.resetWorldSession();state.resetHotkeys();state.resetHotkeyState();
    check(state.nativeTextInputBlocked());state.setNativeTextInputHooksReady(true);
    check(!state.nativeTextInputBlocked());
    for(std::size_t index=kDirectMenuHotkeyFirst;index<kHotkeyCount;++index) {
        check(state.hotkey(index).key==0);check(state.hotkey(index).modifiers==0);
    }
    auto const guiBefore=state.hotkey(0);
    state.bindCapturedHotkey(13,guiBefore.key,guiBefore.modifiers);
    check(state.hotkey(0).key==guiBefore.key);
    check(state.firstHotkeyConflict(13)==0);
    check(state.firstHotkeyConflict(0)==13);
    state.bindCapturedHotkey(12,guiBefore.key,guiBefore.modifiers);
    check(state.hotkey(13).key==guiBefore.key);
    state.clearHotkey(12);state.clearHotkey(13);check(!state.firstHotkeyConflict(0));
    state.setHotkey(13,VK_INSERT,0);
    check(state.tryPressHotkey(13));check(!state.tryPressHotkey(13));
    check(state.releaseHotkeysForKey(VK_INSERT,100));check(state.ignoreHotkeyUntil()>100);
    state.resetHotkeyState();check(state.tryPressHotkey(13));state.releaseHotkey(13);

    auto queue=[&](MenuRoute route){return state.queueMenuRoute({route,state.menuRouteGeneration(),7,1});};
    check(queue(MenuRoute::Verification));check(state.guiVisible());check(state.openingInputBlocked());
    auto intent=state.consumeMenuRoute();check(intent.has_value());check(!state.consumeMenuRoute());
    int applied=0;
    check(intent && state.applyMenuRouteIfCurrent(*intent,false,[&]{++applied;}));check(applied==1);
    state.setGuiVisible(false);auto stale=state.menuRouteGeneration();state.resetWorldSession();
    check(!state.queueMenuRoute({MenuRoute::Placed,stale,7,1}));check(!state.guiVisible());
    check(queue(MenuRoute::Placed));auto older=state.consumeMenuRoute();
    check(queue(MenuRoute::Files));auto newer=state.consumeMenuRoute();
    check(older && !state.applyMenuRouteIfCurrent(*older,false,[&]{++applied;}));
    if(older)state.discardMenuRoute(*older);check(state.guiVisible());
    check(newer && state.applyMenuRouteIfCurrent(*newer,false,[&]{++applied;}));check(applied==2);
    state.setGuiVisible(false);check(queue(MenuRoute::Materials));intent=state.consumeMenuRoute();
    check(intent && !state.applyMenuRouteIfCurrent(*intent,true,[&]{++applied;}));check(!state.guiVisible());
    check(queue(MenuRoute::Verification));state.setNativeTextInputFlag(NativeTextInputFlag::Focus,true);
    check(!state.guiVisible());check(!state.hasPendingMenuRoute());check(!queue(MenuRoute::Files));
    auto oldFocus=state.nativeTextInputToken(NativeTextInputFlag::Focus);
    state.setNativeTextInputFlag(NativeTextInputFlag::Focus,true);
    state.clearNativeTextInputFlagIfCurrent(NativeTextInputFlag::Focus,oldFocus);check(state.nativeTextInputBlocked());
    state.clearNativeTextInputFlagIfCurrent(NativeTextInputFlag::Focus,state.nativeTextInputToken(NativeTextInputFlag::Focus));
    check(queue(MenuRoute::Files));state.setNativeTextInputFlag(NativeTextInputFlag::GameplayDisabled,true);
    check(!state.guiVisible());check(!queue(MenuRoute::Verification));
    state.clearNativeTextInputFlagIfCurrent(NativeTextInputFlag::GameplayDisabled,state.nativeTextInputToken(NativeTextInputFlag::GameplayDisabled));
    check(queue(MenuRoute::Files));state.resetHotkeyState();check(!state.guiVisible());
    state.setGuiVisible(true);check(queue(MenuRoute::Placed));state.cancelMenuRoutes();check(state.guiVisible());
    state.setGuiVisible(false);state.resetHotkeys();state.resetHotkeyState();state.setNativeTextInputHooksReady(false);
    check(applied==2);
}
} // namespace lholo::tests
