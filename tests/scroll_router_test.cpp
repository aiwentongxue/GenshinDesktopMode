#include "../genshin/scroll_state.h"
#include "../genshin/wheel_zoom_policy.h"
#include <cassert>
#include <cmath>
#include <iostream>
static void close(double a,double b){assert(std::abs(a-b)<1e-6);}
static double drain(DesktopWheelRouter &r,double start,unsigned context=2){double n=0;for(int i=1;i<=30;++i)n+=r.frame(start+i/120.,context).y;return n;}
int main(){
 DesktopWheelRouter coarse;coarse.frame(1,2);coarse.hardware(0,1,1.001);
 coarse.system(1,1.001,999,-999,false,0,0,2,false);
 auto first=coarse.frame(1.002,2);assert(first.y>0&&first.y<1);
 close(first.y+drain(coarse,1.002),1);assert(!coarse.usingMos());
 // Raw hardware distance and direction remain authoritative across a burst.
 double wanted=0,actual=0;
 for(int i=0;i<1000;++i){double t=2+i*.001;coarse.hardware(0,.375f,t);wanted+=.375f;coarse.system(t,t,0,-999,false,0,0,2,false);actual+=coarse.frame(t,2).y;}
 close(actual+drain(coarse,2.999),wanted);
 // Ordinary precise input is delivered directly with hardware units.
 DesktopWheelRouter trackpad;trackpad.frame(1,2);trackpad.hardware(0,.375f,1.001);trackpad.system(1,1.001,0,37.5,true,2,0,2,false);
 close(trackpad.frame(1.002,2).y,.375);close(trackpad.frame(1.01,2).y,0);
 // Tagged Mos replaces an undelivered raw notch, without two filters.
 DesktopWheelRouter mos;mos.frame(1,2);mos.hardware(0,1,1.001);mos.system(1,1.002,0,100,true,0,0,2,true);
 assert(mos.usingMos());close(drain(mos,1.002),1);
 // A partially delivered hardware notch finishes once on warm handover.
 DesktopWheelRouter warm;warm.frame(1,2);warm.hardware(0,1,1.001);double n=warm.frame(1.002,2).y;
 assert(n>0&&n<1);warm.system(1,1.01,0,20,true,0,0,2,true);assert(!warm.usingMos());
 n+=warm.frame(1.02,2).y;close(n,1);warm.frame(1.30,2);assert(warm.usingMos());
 warm.system(2,1.31,0,100,true,0,0,2,true);close(drain(warm,1.31),1);
 // Switching Mos OFF recovers the first raw notch in either callback order.
 mos.hardware(0,1,3);mos.system(4,3.001,0,987,false,0,0,2,false);
 n=mos.frame(3.002,2).y;assert(n>0&&n<1);close(n+drain(mos,3.002),1);
 mos.system(5,4,0,-999,false,0,0,2,false);mos.hardware(0,-1,4.001);
 n=mos.frame(4.002,2).y;assert(n<0);close(n+drain(mos,4.002),-1);
 // UI-to-UI changes with different consumers also discard old distance.
 coarse.hardware(0,1,5);assert(coarse.frame(5.001,2,258).y>0);
 close(coarse.frame(5.01,2,1026).y,0);coarse.cancel();close(coarse.frame(6,0).y,0);
    coarse.hardware(0,1,7);assert(coarse.frame(7.001,2).y>0);coarse.cancelCoarse();close(coarse.frame(7.01,2).y,0);
    mos.system(9,8,0,100,true,0,0,2,true);mos.cancelCoarse();close(drain(mos,8),1);
 close(DesktopWheelZoomPolicy::gain(false,0),1);close(DesktopWheelZoomPolicy::gain(true,0),1);
 close(DesktopWheelZoomPolicy::gain(false,1),1.5);close(DesktopWheelZoomPolicy::gain(true,1),4);
 close(DesktopWheelZoomPolicy::gain(false,2),1.8f);close(DesktopWheelZoomPolicy::gain(true,2),4.8f);
 close(DesktopWheelZoomPolicy::gain(false,3),1);close(DesktopWheelZoomPolicy::gain(true,3),4);
 close(DesktopWheelZoomPolicy::gain(false,4),1.5);close(DesktopWheelZoomPolicy::gain(true,4),4);
 std::cout<<"Wheel router: coarse distance, native precise bypass, unchanged Mos timing, warm-up and ON/OFF handover passed\n";
}
