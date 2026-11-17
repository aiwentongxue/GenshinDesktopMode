#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>

// One physical mouse drag is exposed as one Unity Touch. Positions are already
// in Unity screen pixels; this class applies no speed or display-scale factor.
// sample() freezes all reads within a Unity frame. It samples button state, so
// a complete press/release between two samples is intentionally not invented.
class DesktopMapTouchState {
public:
    struct Vector2 {float x=0,y=0;};
    struct Touch {
        int32_t fingerId=0;
        Vector2 position{},rawPosition{},deltaPosition{};
        float deltaTime=0;
        int32_t tapCount=0,phase=0,type=0;
        float pressure=0,maxPressure=0,radius=0,radiusVariance=0,altitude=0,azimuth=0;
    };
    struct Snapshot {int count=0;Touch touch{};};
    enum Phase : int32_t {Began=0,Moved=1,Stationary=2,Ended=3,Cancelled=4};

    bool active() const {return active_;}
    const Snapshot &sample(uint64_t frame,double time,bool eligibleContext,bool foreground,
                           bool leftHeld,bool inside,float x,float y) {
        if(haveFrame_ && frame==frame_) return snapshot_;
        haveFrame_=true;frame_=frame;snapshot_={};
        bool pressed=knowPhysical_ && leftHeld && !previousLeft_;
        // Observe the physical button even outside the map or in the
        // background. Re-entering while held must never restart a cancelled
        // gesture or manufacture a new press.
        knowPhysical_=true;previousLeft_=leftHeld;
        bool validPoint=std::isfinite(x) && std::isfinite(y);
        if(active_) {
            if(!eligibleContext || !foreground || !validPoint) {
                emit(Cancelled,lastX_,lastY_,time);active_=false;
            } else if(!leftHeld) {
                emit(Ended,x,y,time);active_=false;
            } else {
                emit((x==lastX_ && y==lastY_)?Stationary:Moved,x,y,time);
            }
        } else if(pressed && eligibleContext && foreground && inside && validPoint) {
            lastX_=x;lastY_=y;lastTime_=std::isfinite(time)?time:0;
            active_=true;emit(Began,x,y,time);
        }
        return snapshot_;
    }

private:
    Snapshot snapshot_{};
    uint64_t frame_=0;
    bool haveFrame_=false,knowPhysical_=false,previousLeft_=false,active_=false;
    float lastX_=0,lastY_=0;
    double lastTime_=0;

    void emit(Phase phase,float x,float y,double time) {
        Touch touch{};
        touch.position=touch.rawPosition={x,y};
        touch.deltaPosition={x-lastX_,y-lastY_};
        double nextTime=std::isfinite(time)?time:lastTime_;
        // A clock discontinuity may suppress a time delta, never movement or
        // an input edge. Ordinary 30/120 Hz timestamps retain their real gaps.
        double elapsed=nextTime>=lastTime_?nextTime-lastTime_:0;
        touch.deltaTime=phase==Began?0:static_cast<float>(elapsed);
        touch.tapCount=1;touch.phase=phase;
        touch.pressure=phase<=Stationary?1:0;touch.maxPressure=1;
        snapshot_.count=1;snapshot_.touch=touch;
        lastX_=x;lastY_=y;lastTime_=nextTime;
    }
};

static_assert(sizeof(DesktopMapTouchState::Touch)==68,"Unity Touch ABI must occupy 68 bytes");
static_assert(offsetof(DesktopMapTouchState::Touch,position)==4,"Touch position ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,rawPosition)==12,"Touch rawPosition ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,deltaPosition)==20,"Touch deltaPosition ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,deltaTime)==28,"Touch deltaTime ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,tapCount)==32,"Touch tapCount ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,phase)==36,"Touch phase ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,type)==40,"Touch type ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,pressure)==44,"Touch pressure ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,maxPressure)==48,"Touch maxPressure ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,radius)==52,"Touch radius ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,radiusVariance)==56,"Touch radiusVariance ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,altitude)==60,"Touch altitude ABI");
static_assert(offsetof(DesktopMapTouchState::Touch,azimuth)==64,"Touch azimuth ABI");
