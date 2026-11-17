#include "../genshin/control_hotswap_state.h"
#include "../genshin/keyboard_state.h"
#include <cassert>
#include <iostream>
#include <limits>

int main() {
    DesktopHotSwapContext context;
    assert(!context.ready(2)&&!context.ready(3));
    context.initialized=context.foreground=context.owners=true;
    assert(context.ready(2)&&!context.ready(3)); // safe keyboard login/menu return
    context.physicalController=context.nativeJoystick=true;
    assert(!context.ready(3)); // never rebuild controller icons in login
    context.gameplayPage=true;
    assert(context.ready(2)&&context.ready(3)); // includes verified map/model pages
    context.gameplayPage=false;
    assert(context.ready(2)); // HUD hidden must not trap controller users
    context.web=true;assert(!context.ready(2)&&!context.ready(3));
    context.web=false;context.gameplayPage=true;context.foreground=false;
    assert(!context.ready(2)&&!context.ready(3));
    context.foreground=true;context.nativeJoystick=false;assert(!context.ready(3));
    context.nativeJoystick=true;context.physicalController=false;assert(!context.ready(3));
    context.physicalController=true;context.owners=false;assert(!context.ready(2)&&!context.ready(3));
    assert(!context.ready(0)&&!context.ready(1));
    DesktopGamepadIntent pad;
    DesktopGamepadIntent::Sample s;
    assert(!pad.sample(s));
    for(unsigned i=0;i<1000;++i) {s.axes[0]=(i%2?.14f:-.14f);assert(!pad.sample(s));}
    s.axes[0]=.4f;assert(pad.sample(s));
    for(unsigned i=0;i<200;++i) {s.axes[0]=i%2?.28f:.8f;assert(!pad.sample(s));}
    s.axes[0]=0;assert(!pad.sample(s));s.axes[0]=-.5f;assert(pad.sample(s));
    s.buttons=1;assert(pad.sample(s));assert(!pad.sample(s));
    s.buttons=0;assert(!pad.sample(s));s.buttons=2;assert(pad.sample(s));
    pad.reset();assert(!pad.sample(s));assert(!pad.sample(s)); // held on focus/connection is not new
    s={};assert(!pad.sample(s));s.axes[4]=.4f;assert(pad.sample(s));
    s.axes[4]=.2f;assert(!pad.sample(s));s.axes[4]=.4f;assert(!pad.sample(s));
    s.axes[4]=.1f;assert(!pad.sample(s));s.axes[4]=.5f;assert(pad.sample(s));
    s.axes[0]=std::numeric_limits<float>::quiet_NaN();assert(!pad.sample(s));

    DesktopPointerIntent mouse;
    assert(!mouse.motion(0,0,0));assert(!mouse.motion(1,1,0));
    assert(!mouse.motion(1,1,.02));assert(!mouse.motion(1,1,.04));assert(!mouse.motion(1,1,.06));
    assert(mouse.motion(1,1,.08));
    assert(!mouse.motion(100,100,.1)); // one gesture cannot take control back repeatedly
    assert(mouse.motion(6,0,.4));
    assert(!mouse.motion(std::numeric_limits<double>::infinity(),0,.5));
    assert(mouse.wheel(0,1,false,1));
    for(unsigned i=1;i<60;++i)assert(!mouse.wheel(0,.2,i>10,1+i*.016));
    assert(!mouse.wheel(0,.2,true,3)); // a momentum packet never starts a gesture
    assert(mouse.wheel(0,1,false,4));

    DesktopControlHotSwapState state;
    assert(!state.request(0,0));assert(!state.request(3,std::numeric_limits<double>::quiet_NaN()));
    assert(state.request(3,1));assert(state.due(1)==3);
    state.finish(true,1);assert(!state.pending);
    assert(state.request(2,1.05));assert(!state.due(1.1));assert(state.due(1.25)==2);
    auto revision=state.revision;assert(state.request(3,1.1));assert(state.revision!=revision);
    assert(state.due(1.25)==3); // latest source replaces queued opposite mode
    state.manual(2);assert(!state.pending);assert(!state.request(3,2.2));
    assert(state.request(2,2.2,false,true));assert(state.due(2.2)==2);assert(!state.persist);
    state.finish(true,2.2);
    assert(state.request(3,3));
    for(unsigned i=0;i<8;++i)state.finish(false,3+i*.1);
    assert(!state.pending); // refusals are bounded, not an infinite UI loop
    assert(state.request(3,5));assert(!state.due(7.01));
    state.cancel();assert(!state.pending);
    KeyboardState keys;keys.focus(true);keys.reconcile({});keys.pending.clear();
    keys.event(27,true);keys.event(27,false);keys.handoffToKeyboard();
    assert(!keys.held[27]);
    bool escapeDown=false,escapeUp=false;
    for(auto e:keys.pending)if(e.first==27) {if(e.second)escapeDown=true;else if(escapeDown)escapeUp=true;}
    assert(escapeDown&&escapeUp); // a switching tap survives release/clear
    keys.pending.clear();keys.event(97,true);keys.handoffToKeyboard();
    assert(keys.held[97]&&!keys.blockOnNextSnapshot);
    KeyboardState::Keys held{};held[97]=true;keys.reconcile(held);assert(keys.held[97]);
    keys.event(97,false);keys.reconcile({});assert(!keys.held[97]);
    keys.focus(false);keys.focus(true);keys.reconcile(held);assert(keys.blocked[97]);
    keys.handoffToKeyboard();keys.reconcile(held);assert(!keys.held[97]); // focus block remains
    keys.focus(false);keys.focus(true);keys.handoffToKeyboard();
    assert(keys.blockOnNextSnapshot);keys.reconcile(held);assert(!keys.held[97]);
    std::cout<<"Hot-swap: menu keyboard return, login controller guard, web/focus gating, drift, held inputs, momentum, latest request, manual priority, disconnect, Esc/A handoff and bounded retries passed\n";
}
