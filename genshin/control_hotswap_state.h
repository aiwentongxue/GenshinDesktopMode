#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

struct DesktopHotSwapContext {
    bool initialized=false,foreground=false,web=false,owners=false;
    bool gameplayPage=false,physicalController=false,nativeJoystick=false;
    bool ready(int mode) const {
        if(!initialized||!foreground||web||!owners)return false;
        // Returning to keyboard must work even while a menu hides the HUD.
        // Controller action maps still require an actual in-level page.
        if(mode==2)return true;
        return mode==3&&gameplayPage&&physicalController&&nativeJoystick;
    }
};

// Input intent only: this layer never edits a game, controller or cursor.
// One excursion counts once, so drift, a held stick, repeats and momentum
// cannot continually reclaim the active device from a newer input source.
struct DesktopGamepadIntent {
    struct Sample {
        uint32_t buttons=0;
        std::array<float,6> axes{}; // left XY, right XY, left/right trigger
    };
    Sample previous{};
    std::array<bool,4> engaged{};
    bool seeded=false;
    void reset() { *this={}; }
    bool sample(Sample value) {
        for(auto &axis:value.axes) if(!std::isfinite(axis))axis=0;
        std::array<float,4> magnitude={
            std::hypot(value.axes[0],value.axes[1]),
            std::hypot(value.axes[2],value.axes[3]),value.axes[4],value.axes[5]};
        bool fresh=seeded&&(value.buttons&~previous.buttons)!=0;
        for(unsigned i=0;i<4;++i) {
            float on=i<2?.30f:.35f,off=.16f;
            if(!seeded) engaged[i]=magnitude[i]>off;
            else if(magnitude[i]<=off)engaged[i]=false;
            else if(!engaged[i]&&magnitude[i]>=on) {engaged[i]=true;fresh=true;}
        }
        seeded=true;previous=value;return fresh;
    }
};

struct DesktopPointerIntent {
    double lastMotion=-100,lastScroll=-100,distance=0;
    bool motionUsed=false;
    void reset() { *this={}; }
    bool motion(double dx,double dy,double now) {
        if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(now)||(!dx&&!dy))return false;
        if(now-lastMotion>.15) {distance=0;motionUsed=false;}
        lastMotion=now;distance+=std::hypot(dx,dy);
        if(!motionUsed&&distance>=6) {motionUsed=true;return true;}
        return false;
    }
    bool wheel(double dx,double dy,bool momentum,double now) {
        if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(now)||(!dx&&!dy))return false;
        bool began=now-lastScroll>.25;lastScroll=now;
        return began&&!momentum;
    }
};

struct DesktopControlHotSwapState {
    int pending=0;
    bool persist=true;
    unsigned attempts=0;
    double began=0,nextAttempt=0,holdUntil=0,lastSwitch=-100;
    uint64_t revision=0;
    void cancel() {pending=0;attempts=0;++revision;}
    void manual(double now) {cancel();holdUntil=now+.4;lastSwitch=now;}
    bool request(int mode,double now,bool remember=true,bool fallback=false) {
        if((mode!=2&&mode!=3)||!std::isfinite(now)||(!fallback&&now<holdUntil))return false;
        if(pending==mode&&persist==remember)return true;
        cancel();pending=mode;persist=remember;began=now;
        nextAttempt=fallback?now:std::max(now,lastSwitch+.25);
        return true;
    }
    int due(double now) {
        if(pending&&now-began>2)cancel();
        return pending&&now>=nextAttempt?pending:0;
    }
    void finish(bool accepted,double now) {
        if(accepted) {lastSwitch=now;cancel();}
        else if(++attempts>=8)cancel();
        else nextAttempt=now+.1;
    }
};
