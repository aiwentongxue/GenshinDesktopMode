#include "../genshin/map_touch_state.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using State=DesktopMapTouchState;
static bool Near(float a,double b) {return std::fabs(a-b)<0.00001;}
static void FrameRates() {
    for(double hz:{30.0,120.0}) {
        State state;double time=10,dt=1/hz;
        assert(!state.sample(0,time,true,true,false,true,100,200).count);
        auto began=state.sample(1,time+=dt,true,true,true,true,100,200);
        assert(began.count==1 && began.touch.phase==State::Began);
        assert(began.touch.deltaTime==0 && began.touch.deltaPosition.x==0);
        assert(began.touch.position.x==began.touch.rawPosition.x);
        assert(began.touch.fingerId==0 && began.touch.tapCount==1 && began.touch.type==0);
        assert(began.touch.pressure==1 && began.touch.maxPressure==1);
        assert(began.touch.radius==0 && began.touch.radiusVariance==0 && began.touch.altitude==0 && began.touch.azimuth==0);
        // Mutating every input in the same frame cannot consume an edge or
        // position again. Both count and GetTouch readers see identical data.
        auto again=state.sample(1,time+10,false,false,false,false,999,999);
        assert(!std::memcmp(&began,&again,sizeof(began)));
        for(uint64_t frame=2;frame<=5;++frame) {
            auto moved=state.sample(frame,time+=dt,true,true,true,true,100+float(frame-1)*3,200);
            assert(moved.count==1 && moved.touch.phase==State::Moved);
            assert(moved.touch.deltaPosition.x==3 && moved.touch.deltaPosition.y==0);
            assert(Near(moved.touch.deltaTime,dt));
        }
        auto still=state.sample(6,time+=dt,true,true,true,true,112,200);
        assert(still.touch.phase==State::Stationary && still.touch.deltaPosition.x==0);
        assert(Near(still.touch.deltaTime,dt));
        // Release includes the final physical displacement, once.
        auto ended=state.sample(7,time+=dt,true,true,false,true,114,201);
        assert(ended.count==1 && ended.touch.phase==State::Ended && !state.active());
        assert(ended.touch.deltaPosition.x==2 && ended.touch.deltaPosition.y==1);
        assert(ended.touch.pressure==0 && Near(ended.touch.deltaTime,dt));
        assert(state.sample(7,time,true,true,false,true,114,201).touch.phase==State::Ended);
        assert(!state.sample(8,time+=dt,true,true,false,true,114,201).count);
    }
}
static void BoundariesAndCancellation() {
    State state;
    // The first observation might already be held at startup; require a fresh
    // release/press, including when entering the map from another interface.
    assert(!state.sample(1,1,false,true,true,true,10,10).count);
    assert(!state.sample(2,2,true,true,true,true,10,10).count);
    assert(!state.sample(3,3,true,true,false,true,10,10).count);
    assert(state.sample(4,4,true,true,true,true,10,10).touch.phase==State::Began);
    auto outside=state.sample(5,4.02,true,true,true,false,-400,3000);
    assert(outside.touch.phase==State::Moved && state.active());
    assert(outside.touch.position.x==-400 && outside.touch.position.y==3000);
    assert(outside.touch.deltaPosition.x==-410 && Near(outside.touch.deltaTime,.02));
    auto cancelled=state.sample(6,4.10,true,false,true,false,9999,9999);
    assert(cancelled.count==1 && cancelled.touch.phase==State::Cancelled && !state.active());
    assert(cancelled.touch.position.x==-400 && cancelled.touch.deltaPosition.x==0);
    assert(Near(cancelled.touch.deltaTime,.08));
    assert(!state.sample(7,4.2,true,true,true,true,10,10).count);
    assert(!state.sample(8,4.3,true,true,true,true,10,10).count);
    state.sample(9,4.4,true,true,false,true,10,10);
    assert(state.sample(10,4.5,true,true,true,true,10,10).touch.phase==State::Began);
    auto contextCancelled=state.sample(11,4.6,false,true,true,true,11,10);
    assert(contextCancelled.touch.phase==State::Cancelled);
    assert(!state.sample(12,4.7,false,true,true,true,11,10).count);
    assert(!state.sample(13,4.8,true,true,true,true,11,10).count);
    state.sample(14,4.9,true,true,false,true,11,10);
    // Pressing outside then dragging in does not gain ownership.
    assert(!state.sample(15,5,true,true,true,false,11,10).count);
    assert(!state.sample(16,5.1,true,true,true,true,11,10).count);
    state.sample(17,5.2,true,true,false,true,11,10);
    assert(state.sample(18,5.3,true,true,true,true,11,10).touch.phase==State::Began);
}
static void VariableTimingAndSnapshotContract() {
    State state;state.sample(0,1,true,true,false,true,0,0);
    state.sample(1,1.003,true,true,true,true,1,1);
    auto moved=state.sample(2,1.044,true,true,true,true,2,1);
    assert(Near(moved.touch.deltaTime,.041));
    auto paused=state.sample(3,2.044,true,true,true,true,2,1);
    assert(paused.touch.phase==State::Stationary && Near(paused.touch.deltaTime,1));
    auto released=state.sample(4,2.05,true,true,false,true,2,1);
    assert(released.touch.phase==State::Ended && Near(released.touch.deltaTime,.006));
    assert(!state.sample(5,2.06,true,true,false,true,2,1).count);
    // A complete click between samples cannot be reconstructed from two
    // released snapshots. This is the documented snapshot-only constraint.
    assert(!state.sample(6,2.07,true,true,false,true,2,1).count);
}
int main() {
    FrameRates();BoundariesAndCancellation();VariableTimingAndSnapshotContract();
    std::cout<<"Map touch: frozen frames, real 30/120Hz timing, one-shot ends/cancels, focus/context ownership and unscaled positions passed\n";
}
