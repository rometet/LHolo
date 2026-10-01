#pragma once
#include "overlay/GameMouseHandoffRequest.h"
#include <Windows.h>
#include <array>
#include <thread>
#include <future>

namespace lholo::tests::mouse_handoff {
using overlay::detail::GameMouseHandoffRequest;
using overlay::detail::MouseHandoffIdentity;
using overlay::detail::MouseHandoffMessage;
inline constexpr UINT kQueue=WM_APP+0x210;
inline constexpr UINT kRestore=WM_APP+0x211;
inline constexpr UINT kDrain=WM_APP+0x212;
inline constexpr UINT kTransition=WM_APP+0x213;
struct State {
    GameMouseHandoffRequest request;
    MouseHandoffIdentity identity;
    bool foreground{true};
    unsigned restores{}, cancels{}, messages{};
    DWORD owner{};
    unsigned centers{}, wrongThreadEffects{};
};
inline LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message==WM_NCCREATE) {
        auto* c=reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(c->lpCreateParams));
    }
    auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (!s) return DefWindowProcW(window,message,wParam,lParam);
    if (message==WM_KILLFOCUS || (message==WM_ACTIVATEAPP && !wParam)) {
        s->foreground=false;
        s->request.cancel();
        ++s->cancels;
        return 0;
    }
    if (message==WM_SETFOCUS) { s->foreground=true; return 0; }
    if (message==kQueue) {
        if (!s->request.begin(s->identity,s->request.cancellationEpoch())) return 0;
        auto q=s->request.queue(s->identity);
        if (!q || !PostMessageW(window,kRestore,q->ticket,static_cast<LPARAM>(q->session))) return 0;
        // Sent focus messages overtake the posted private restoration. Focus
        // regain is deliberately delivered before that old restoration too.
        if (wParam==1 || wParam==2) SendMessageW(window,WM_KILLFOCUS,0,0);
        if (wParam==3 || wParam==4) SendMessageW(window,WM_ACTIVATEAPP,FALSE,0);
        if (wParam==2 || wParam==4) SendMessageW(window,WM_SETFOCUS,0,0);
        return PostMessageW(window,kDrain,0,0)!=FALSE;
    }
    if (message==kRestore) {
        ++s->messages;
        if (s->request.consume({static_cast<std::uintptr_t>(wParam),static_cast<std::uintptr_t>(lParam)},
                s->identity,s->foreground)) ++s->restores;
        return 0;
    }
    if (message==kTransition) {
        ++s->messages;
        if (s->request.consumeTransition({static_cast<std::uintptr_t>(wParam),static_cast<std::uintptr_t>(lParam)},
                s->identity,s->foreground)) {
            if (overlay::detail::windowThreadMouseHandoffAllowed(GetCurrentThreadId(),s->owner)) ++s->centers;
            else ++s->wrongThreadEffects;
        }
        return 0;
    }
    if (message==kDrain) return 0;
    return DefWindowProcW(window,message,wParam,lParam);
}
template<class Check>
void runGameMouseHandoffChecks(Check&& check) {
    using overlay::detail::foregroundMouseHandoffWindow;
    using overlay::detail::gameMouseHandoffAllowed;
    using overlay::detail::windowThreadMouseHandoffAllowed;
    check(windowThreadMouseHandoffAllowed(22,22),"production cursor effect predicate accepts owner thread");
    check(!windowThreadMouseHandoffAllowed(23,22),"production cursor effect predicate rejects Present/worker thread");
    check(!windowThreadMouseHandoffAllowed(0,0),"production cursor effect predicate rejects missing owner");
    check(foregroundMouseHandoffWindow(11,11,true,true,false),"current live visible foreground window accepted");
    check(!foregroundMouseHandoffWindow(11,12,true,true,false),"background window fails production foreground predicate");
    check(!foregroundMouseHandoffWindow(0,0,true,true,false),"missing window fails production foreground predicate");
    check(!foregroundMouseHandoffWindow(11,11,false,true,false),"destroyed window fails production foreground predicate");
    check(!foregroundMouseHandoffWindow(11,11,true,false,false),"hidden window fails production foreground predicate");
    check(!foregroundMouseHandoffWindow(11,11,true,true,true),"minimized window fails production foreground predicate");
    check(gameMouseHandoffAllowed(true,false,false,true),"foreground gameplay restore readiness accepted");
    check(!gameMouseHandoffAllowed(false,false,false,true),"production restore readiness rejects lost foreground");
    check(!gameMouseHandoffAllowed(true,true,false,true),"production restore readiness rejects open menu");
    check(!gameMouseHandoffAllowed(true,false,true,true),"production restore readiness rejects shutdown");
    check(!gameMouseHandoffAllowed(true,false,false,false),"production restore readiness rejects inventory/non-gameplay screen");
    GameMouseHandoffRequest r;
    MouseHandoffIdentity id{11,22,33};
    r.resetWindow(id.window,100);
    auto queued=[&] { r.begin(id,r.cancellationEpoch()); return *r.queue(id); };
    auto message=queued();
    check(r.consume(message,id,true),"foreground HUD handoff accepted exactly once");
    check(!r.consume(message,id,true),"duplicate private handoff cannot reclip/regrab");
    message=queued(); r.cancel();
    check(!r.consume(message,id,true),"focus loss cancels queued request even after focus regain");
    message=queued();
    check(!r.consume(message,id,false),"background/inventory/menu/shutdown gate denies restoration");
    check(!r.consume(message,id,true),"denied request cannot revive with foreground");
    message=queued();
    check(!r.consume(message,{12,22,33},true),"other window cannot consume restoration");
    message=queued();
    check(!r.consume(message,{11,23,33},true),"client replacement invalidates queued restoration");
    message=queued();
    check(!r.consume(message,{11,22,34},true),"local player replacement invalidates queued restoration");
    auto old=queued();
    message=queued();
    check(!r.consume(old,id,true) && r.consume(message,id,true),"old ticket cannot retire fresh request");
    old=queued(); r.resetWindow(id.window,101);
    message=queued();
    check(!r.consume(old,id,true) && r.consume(message,id,true),"overlay installation session rejects stale message");
    message=queued(); r.deliveryFailed(message);
    check(r.transitionCurrent(id),"failed PostMessage preserves transition for bounded next-frame retry");
    auto retry=r.queue(id);
    check(retry && r.consume(*retry,id,true),"successful retry consumes retained handoff");
    r.resetWindow(0,0);
    check(!r.begin(id,r.cancellationEpoch()),"shutdown retires handoff window/session");
    r.resetWindow(id.window,102);
    check(!r.begin({11,0,33},r.cancellationEpoch()) && !r.begin({11,22,0},r.cancellationEpoch()),"missing client/player cannot begin gameplay handoff");
    r.begin(id,r.cancellationEpoch());
    auto transition=r.queueTransition(id);
    check(transition && !r.queueTransition(id),"transition posting bounded to one outstanding message");
    check(r.consumeTransition(*transition,id,true),"owner accepts current transition centering request");
    check(!r.consumeTransition(*transition,id,true),"duplicate transition cannot perform cursor effects");
    transition=r.queueTransition(id); r.transitionDeliveryFailed(*transition);
    transition=r.queueTransition(id);
    check(transition.has_value(),"failed transition PostMessage permits bounded retry");
    auto final=r.queue(id);
    check(final && !r.consumeTransition(*transition,id,true) && r.consume(*final,id,true),
        "final restore retires outstanding transition message without disturbing final ticket");
    r.begin(id,r.cancellationEpoch()); transition=r.queueTransition(id); r.cancel();
    check(!r.consumeTransition(*transition,id,true),"focus loss retires transition even if foreground returns");
    r.begin(id,r.cancellationEpoch()); transition=r.queueTransition(id);
    check(!r.consumeTransition(*transition,{11,22,34},true) && !r.transitionCurrent(id),
        "player replacement retires pending transition");

    State s;
    WNDCLASSW klass{};
    klass.lpfnWndProc=procedure;
    klass.hInstance=GetModuleHandleW(nullptr);
    klass.lpszClassName=L"LHoloMouseHandoffQueueChecks";
    auto atom=RegisterClassW(&klass);
    check(atom!=0,"register isolated production handoff queue fixture");
    if (!atom) return;
    HWND window=CreateWindowW(klass.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,klass.hInstance,&s);
    check(window!=nullptr,"create isolated handoff fixture window");
    if (!window) { UnregisterClassW(klass.lpszClassName,klass.hInstance); return; }
    s.identity={reinterpret_cast<std::uintptr_t>(window),22,33};
    s.owner=GetCurrentThreadId();
    s.request.resetWindow(s.identity.window,200);
    for(unsigned scenario=0;scenario<5;++scenario) {
        s.foreground=true;
        auto before=s.restores;
        auto messagesBefore=s.messages;
        auto cancelsBefore=s.cancels;
        check(SendMessageW(window,kQueue,scenario,0)!=0,"post restoration then deliver selected sent focus messages");
        MSG m{};
        while (PeekMessageW(&m,window,0,0,PM_REMOVE)) DispatchMessageW(&m);
        check(s.messages==messagesBefore+1,"queued restoration is actually dispatched after focus sequence");
        check(s.restores==before+(scenario==0?1U:0U),"only foreground uncancelled restoration performs cursor/grab effects");
        check(s.cancels==cancelsBefore+(scenario==0?0U:1U),"KILLFOCUS/ACTIVATEAPP(false) invalidation precedes queued delivery");
    }
    for(unsigned scenario=0;scenario<5;++scenario) {
        s.foreground=true;
        auto before=s.centers;
        auto messagesBefore=s.messages;
        bool posted=false, bounded=false, producerDenied=false;
        // Present-like producer never clips/centers directly. Its queued owner
        // request is overtaken by the same sent focus messages as production.
        std::thread producer([&] {
            producerDenied=!windowThreadMouseHandoffAllowed(GetCurrentThreadId(),s.owner);
            s.request.begin(s.identity,s.request.cancellationEpoch());
            auto q=s.request.queueTransition(s.identity);
            bounded=!s.request.queueTransition(s.identity);
            posted=q && PostMessageW(window,kTransition,q->ticket,static_cast<LPARAM>(q->session));
        });
        producer.join();
        check(posted && bounded && producerDenied,"render producer posts one request and cannot perform native cursor effects");
        if (scenario==1 || scenario==2) SendMessageW(window,WM_KILLFOCUS,0,0);
        if (scenario==3 || scenario==4) SendMessageW(window,WM_ACTIVATEAPP,FALSE,0);
        if (scenario==2 || scenario==4) SendMessageW(window,WM_SETFOCUS,0,0);
        MSG m{};
        while (PeekMessageW(&m,window,0,0,PM_REMOVE)) DispatchMessageW(&m);
        check(s.messages==messagesBefore+1,"owner dispatches actual queued transition after sent focus sequence");
        check(s.centers==before+(scenario==0?1U:0U),"focus cancellation prevents cross-thread delayed centering/reclip");
        check(s.wrongThreadEffects==0,"all accepted cursor effects occur on fixture WndProc owner");
    }
    {
        s.foreground=true;
        std::promise<void> sampled, resume;
        auto sampledSignal=sampled.get_future();
        auto resumeSignal=resume.get_future();
        bool admitted=true;
        std::thread producer([&] {
            auto epoch=s.request.cancellationEpoch();
            // Pause at the production foreground-read -> begin admission gap.
            sampled.set_value();
            resumeSignal.wait();
            admitted=s.request.begin(s.identity,epoch);
        });
        sampledSignal.wait();
        SendMessageW(window,WM_KILLFOCUS,0,0);
        SendMessageW(window,WM_SETFOCUS,0,0);
        resume.set_value();
        producer.join();
        check(!admitted && !s.request.transitionCurrent(s.identity),
            "focus loss/regain between foreground sampling and begin rejects stale render intent");
    }
    DestroyWindow(window);
    UnregisterClassW(klass.lpszClassName,klass.hInstance);
}
} // namespace lholo::tests::mouse_handoff
