#include "../genshin/scroll_state.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

static void close(double a,double b) {assert(std::abs(a-b)<1e-9);}
int main() {
    for(int hz:{30,60,120,240}) {
        DesktopScrollState state;
        state.frame(1,1);
        assert(state.event(5,1,0,100,true,0,0,1));
        double total=0;
        for(int i=1;i<=hz;++i) total+=state.frame(1+double(i)/hz,1).y;
        close(total,1);
        close(state.frame(3,1).y,0);
    }
    // Hundreds of independent Mos pulses may share a timestamp. Preserve all
    // their distance, including fractions far below one game wheel notch.
    DesktopScrollState state;state.frame(1,2);double total=0;
    for(int i=1;i<=120;++i) {
        double t=1+double(i)/120;
        assert(state.event(9,t,0,0.03125,true,0,0,2));
        total+=state.frame(t,2).y;
    }
    for(int i=1;i<=120;++i) total+=state.frame(2+double(i)/120,2).y;
    close(total,120*0.03125/100);
    // A reversal is immediate rather than competing with an old tail.
    state.event(10,3,0,100,true,0,0,2);state.frame(3,2);
    state.event(11,3.01,0,-100,true,0,0,2);
    assert(state.frame(3.02,2).y<0);
    // Closing a menu/map cannot replay its Mos tail into the world camera.
    state.context(1);close(state.frame(3.03,1).y,0);
    assert(!state.event(11,3.04,0,-5,true,0,0,1));
    assert(state.event(12,3.05,0,5,true,0,0,1));
    assert(state.frame(3.06,1).y>0);
    // Trackpad momentum also remains cancelled until a fresh gesture.
    DesktopScrollState trackpad;trackpad.frame(1,2);
    trackpad.event(1,1,0,10,true,1,0,2);trackpad.frame(1.01,2);
    trackpad.context(1);
    assert(!trackpad.event(2,1.02,0,10,true,0,4,1));
    assert(trackpad.event(3,1.03,0,10,true,1,0,1));
    assert(trackpad.frame(1.04,1).y>0);
    trackpad.cancel();close(trackpad.frame(1.05,0).y,0);
    assert(!trackpad.event(3,1.06,0,10,true,0,0,1));
    assert(!trackpad.event(4,1.07,0,std::numeric_limits<double>::infinity(),true,0,0,1));
    // Nested AppKit dispatch is one delivery. New outer frames are preserved
    // even when all their event values and timestamps are exactly identical.
    int accepted=0;
    DesktopScrollDispatchScope::Key key{7,0,1,0,0,true};
    for(int i=0;i<1000;++i) {
        DesktopScrollDispatchScope app(key);accepted+=app.first;
        DesktopScrollDispatchScope window(key);assert(!window.first);
        DesktopScrollDispatchScope monitor(key);assert(!monitor.first);
    }
    assert(accepted==1000);
    DesktopScrollState coarse;coarse.frame(1,1);
    coarse.event(1,1,0,1,false,0,0,1);
    close(coarse.frame(1.01,1).y,1); // smoothing disabled: ordinary wheel stays immediate
    close(coarse.frame(1.02,1).y,0);
    std::cout<<"Scroll: 30/60/120/240 Hz distance preserved; fractional Mos pulses, reversal, context/focus cancellation and nested dispatch passed\n";
}
