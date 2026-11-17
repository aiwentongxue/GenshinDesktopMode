#include "pointer_motion.h"
#include "pointer_touch.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static void expect(DesktopMotion::Point actual,double x,double y) {
    assert(std::abs(actual.x-x)<0.000001);
    assert(std::abs(actual.y-y)<0.000001);
}
int main() {
    DesktopMotion motion;
    motion.reset({100,100});
    // Preserve accelerated fractional movement exactly with no warp.
    expect(motion.consume({0.375,-2.25},{100,100}),0.375,-2.25);
    // Moving the cursor ourselves must not amplify the next real movement.
    motion.didWarp({125,90});
    expect(motion.consume({30,-14},{125,90}),5,-4);
    // Fast movement into a clipped edge, followed by a reversal.
    motion.didWarp({130,86});
    expect(motion.consume({205,-4},{130,86}),200,0);
    motion.didWarp({140,86});
    expect(motion.consume({7,0},{140,86}),-3,0);
    // Centering and another warp before an event use the final destination.
    motion.didWarp({500,500});motion.didWarp({550,450});
    expect(motion.consume({410,364},{550,450}),0,0);
    // Focus release/recapture must discard previous warp correction.
    motion.didWarp({800,800});motion.reset({40,40});
    expect(motion.consume({-2.125,1.75},{40,40}),-2.125,1.75);
    // Correct the one pre-drag warp, then preserve movement with no per-event
    // warp during dragging. Synchronize the physical point once on release.
    motion.didWarp({200,100});
    expect(motion.consume({175.25,56.5},{200,100}),15.25,-3.5);
    expect(motion.consume({-8.5,2.25},{200,100}),-8.5,2.25);
    motion.didWarp({206.75,98.75});
    expect(motion.consume({9.75,0.75},{206.75,98.75}),3,2);
    // Repeated cursor relocation cannot add drift to a physical path.
    motion.reset({0,0});
    double previousX=0,previousY=0,positionX=0,positionY=0,totalX=0,totalY=0;
    for(int i=0;i<10000;++i) {
        const double dx=(i%2)?-1.25:1.25,dy=(i%4<2)?0.375:-0.375;
        auto result=motion.consume({dx+positionX-previousX,dy+positionY-previousY},{positionX,positionY});
        expect(result,dx,dy);totalX+=result.x;totalY+=result.y;
        previousX=positionX;previousY=positionY;
        positionX+=result.x;positionY+=result.y;
        motion.didWarp({positionX,positionY});
    }
    assert(totalX==0 && totalY==0);
    // UIKit may deliver old and translated mouse positions in alternation.
    // The drag history must follow the shared pointer on every delivery.
    DesktopTouchHistory touch;
    const int firstTouch=1,secondTouch=2;
    touch.begin(&firstTouch,{100,100});
    assert(touch.update(&firstTouch,{140,125}));
    assert(touch.current.x-touch.previous.x==40 && touch.current.y-touch.previous.y==25);
    assert(touch.update(&firstTouch,{140,125}));
    assert(touch.current.x==touch.previous.x && touch.current.y==touch.previous.y);
    assert(!touch.update(&secondTouch,{0,0}));
    assert(touch.update(&firstTouch,{130,120}));
    assert(touch.current.x-touch.previous.x==-10 && touch.current.y-touch.previous.y==-5);
    touch.reset();assert(!touch.update(&firstTouch,{200,200}));
    touch.begin(&secondTouch,{500,400});
    assert(touch.previous.x==500 && touch.current.y==400);
    puts("System movement preservation, warp correction, edge reversal and focus reset passed.");
    puts("Drag position history, duplicate touch delivery and touch identity/reset passed.");
}
