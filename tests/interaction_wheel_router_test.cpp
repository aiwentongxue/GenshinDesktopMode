#include "../genshin/scroll_state.h"
#include <cassert>
#include <cmath>
#include <iostream>
static void equal(double a,double b){assert(std::abs(a-b)<1e-6);}
int main(){
    // A native selection threshold cannot see a notch split across frames.
    DesktopWheelRouter previous;
    previous.hardware(0,1,1);
    double peak=0,total=0;
    for(int i=0;i<20;++i){double y=previous.frame(1+i/120.,1).y;peak=std::max(peak,y);total+=y;}
    equal(total,1);assert(peak*1.8<1);
    // Reproduce the real failure: GC reports the first notch, then only
    // AppKit reports the following identical 0.1-line packets. Every packet
    // must produce one selection in either direction, even with equal stamps.
    for(float direction:{1.f,-1.f}) {
        DesktopWheelRouter r;
        for(int i=0;i<20;++i) {
            double t=2+i*.1;
            if(i==0)r.hardware(0,direction,t);
            r.system(123,t,0,.1*direction,false,0,0,1,false);
            assert(r.hasPendingInput());
            equal(r.frame(t,1,1,true).y,direction);
            assert(!r.hasPendingInput());
            equal(r.frame(t+.01,1,1,true).y,0);
        }
    }
    // Callback order may straddle engine frames. GC never owns an ordinary
    // selection, so neither ordering can duplicate a notch.
    for(bool gcFirst:{false,true}) {
        DesktopWheelRouter r;
        if(gcFirst) {
            r.hardware(0,1,5);equal(r.frame(5,1,1,true).y,0);
            r.system(5,5.01,0,.1,false,0,0,1,false);
            equal(r.frame(5.01,1,1,true).y,1);
        } else {
            r.system(5,5,0,.1,false,0,0,1,false);
            equal(r.frame(5,1,1,true).y,1);
            r.hardware(0,1,5.01);equal(r.frame(5.01,1,1,true).y,0);
        }
    }
    // Several distinct events in one engine frame and integral coalesced
    // line counts retain their distance without the camera zoom gain.
    DesktopWheelRouter r;
    for(int i=0;i<4;++i)r.system(6,6,0,-.1,false,0,0,1,false);
    equal(r.frame(6,1,1,true).y,-4);
    r.system(6.1,6.1,0,.8142,false,0,0,1,false,0,3);
    equal(r.frame(6.1,1,1,true).y,3);
    equal(DesktopWheelRouter::selectionUnits(0,5),0);
    equal(DesktopWheelRouter::selectionUnits(-.1,0),-1);
    equal(DesktopWheelRouter::selectionUnits(.1,3),3);
    equal(DesktopWheelRouter::selectionUnits(.1,1000),32);
    equal(DesktopWheelRouter::selectionUnits(NAN,1),0);
    // Both physical/system callback orders and the full Mos tail produce one
    // original notch, never a copy or a fractional synthetic selection.
    for(bool physicalFirst:{false,true}){
        DesktopWheelRouter m;
        if(physicalFirst)m.hardware(0,1,4);
        m.system(4,4,0,20,true,0,0,1,true);
        if(!physicalFirst)m.hardware(0,1,4);
        equal(m.frame(4,1,1,true).y,1);assert(!m.usingMos());
        for(int i=1;i<10;++i){m.system(4+i/120.,4+i/120.,0,10,true,0,0,1,true);equal(m.frame(4+i/120.,1,1,true).y,0);}
        // Opening a map discards selection history; a fresh notch still gets
        // the original map smoothing/distance behavior.
        equal(m.frame(5,2,258,false).y,0);
        m.system(5,5.001,0,1,false,0,0,2,false);m.hardware(0,1,5.001);
        double sum=m.frame(5.002,2,258).y;assert(sum>0&&sum<1);
        for(int i=1;i<30;++i)sum+=m.frame(5.002+i/120.,2,258).y;
        equal(sum,1);
    }
    // Entering the list drops a pending world-camera tail, even with no new
    // hardware input. A new physical notch still arrives in full.
    r.frame(7,1);r.hardware(0,1,7.001);assert(r.frame(7.002,1).y>0);
    equal(r.frame(7.01,1,1,true).y,0);
    r.system(7.02,7.02,0,-.1,false,0,0,1,false);equal(r.frame(7.021,1,1,true).y,-1);
    r.system(8,8,0,.1,false,0,0,1,false);r.cancel();equal(r.frame(8,1,1,true).y,0);
    r.system(9,9,0,.1,false,0,0,1,false);equal(r.frame(9,0,0,true).y,0);equal(r.frame(9.01,1,1,true).y,0);
    r.system(10,10,25,50,true,2,0,1,false);r.hardware(.25f,.5f,10);
    auto precise=r.frame(10,1,1,true);equal(precise.x,.25);equal(precise.y,.5);
    r.system(11,11,0,.1,false,0,0,1,false);r.cancelCoarse();equal(r.frame(11,1,1,true).y,0);
    // System-only input outside F lists cannot inject additional camera or
    // map distance. Opening a map also drops queued world-selection packets.
    DesktopWheelRouter normal;
    normal.system(12,12,0,.1,false,0,0,1,false);
    equal(normal.frame(12,1,1,false).y,0);
    normal.system(13,13,0,.1,false,0,0,1,false);
    equal(normal.frame(13,2,258,false).y,0);
    equal(normal.frame(13.01,1,1,true).y,0);
    std::cout<<"PASS identical system-only notches, both directions, frame-split callback dedup, coalesced lines, Mos isolation, map restoration, focus cancellation\n";
}
