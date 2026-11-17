#pragma once
#include <array>
#include <cmath>
#include <algorithm>

// Rewired durations are seconds; zero means until a StopVibration request.
// The SDK pulse is a separate source so it cannot cancel a native motor.
struct ControllerHapticState {
    struct Motor {float level=0,sharpness=0;double deadline=0;};
    std::array<Motor,3> motors{}; // left, right, SDK pulse
    static float unit(float value) {
        return std::isfinite(value)?std::max(0.f,std::min(1.f,value)):0.f;
    }
    void stop() {motors={};}
    bool set(unsigned index,float level,double duration,double now,bool stopOther=false) {
        if(index>1 || !std::isfinite(duration) || duration<0 || !std::isfinite(now)) return false;
        if(stopOther) motors[1-index]={};
        level=unit(level);
        motors[index]={level,index==0?.2f:.8f,level>0&&duration>0?now+duration:0};
        return true;
    }
    bool pulse(long long milliseconds,float level,float sharpness,double now) {
        if(milliseconds<=0 || !std::isfinite(now)) return false;
        level=unit(level);
        motors[2]={level,unit(sharpness),level>0?now+std::min(milliseconds,5000LL)/1000.:0};
        return true;
    }
    void expire(double now) {
        for(auto &motor:motors)
            if(motor.deadline>0 && motor.deadline<=now) motor={};
    }
    Motor output(unsigned side) const {
        Motor motor=motors[side];
        return motors[2].level>motor.level?motors[2]:motor;
    }
    double nextDeadline() const {
        double next=0;
        for(auto motor:motors) if(motor.deadline>0 && (!next||motor.deadline<next)) next=motor.deadline;
        return next;
    }
};
