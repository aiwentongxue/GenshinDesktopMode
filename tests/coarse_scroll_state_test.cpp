#include "../genshin/coarse_scroll_state.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <cstdio>
#include <initializer_list>
int main(){
 for(int hz:{30,60,120,240}){
  DesktopCoarseScrollState s;s.frame(1,1);s.event(1.001,2,3,1);
  double x=0,y=0,peak=0;
  for(int i=1;i<=hz;++i){auto v=s.frame(1+double(i)/hz,1);x+=v.x;y+=v.y;peak=std::fmax(peak,std::fabs(v.y));if(i==1)assert(v.y>0&&v.y<3);}
  assert(std::fabs(x-2)<1e-10&&std::fabs(y-3)<1e-10&&peak<3&&!s.pending());
 }
 // Rapid notches retain their combined distance instead of turning a fixed
 // interval into a new speed/acceleration multiplier.
 DesktopCoarseScrollState s;s.frame(1,1);double total=0;
 for(int i=0;i<50;++i){double t=1.001+i*.01;s.event(t,0,1,1);total+=s.frame(t,1).y;}
 for(int i=1;i<=60;++i)total+=s.frame(1.491+i/120.,1).y;
 assert(std::fabs(total-50)<1e-9);
 // The finite reconstruction is zero at the event timestamp; it begins on
 // the first frame with positive elapsed time, including after a pause.
 s.event(3,0,1,1);assert(s.frame(3.0001,1).y>0);
 s.event(3.001,0,-1,1);assert(s.frame(3.01,1).y<0);
 // Closing a page, switching UI/camera, losing focus and long suspension must
 // never replay the old page's remainder into its successor.
 s.event(4,0,1,1);assert(s.frame(4.001,2).y==0);
 s.event(5,0,1,2);s.cancel();assert(s.frame(5.01,2).y==0);
 s.event(6,0,1,2);s.frame(6.01,2);assert(s.frame(7,2).y==0);
 s.event(8,0,1,2);s.frame(8.01,2);s.event(9,0,2,2);assert(s.frame(9.0001,2).y>0);
 s.cancel();s.event(10,0,1,2);assert(s.frame(std::numeric_limits<double>::quiet_NaN(),2).y==0);
 assert(!s.pending());
 std::puts("Coarse wheel: first-frame response, bounded interpolation, total distance, reversal and page/focus/pause cancellation passed");
}
