#pragma once
#include <cmath>
#include <cstdint>
#include "coarse_scroll_state.h"

// Frame-rate-independent delivery of wheel distance. Precise system packets
// are already smooth; a short reconstruction window fills the gaps between
// display-link events and engine frames without another long inertia curve.
class DesktopScrollState {
public:
    struct Delta {double x=0,y=0;};
    static constexpr double precisePointsPerUnit=100.0;
    void cancel() {
        pending_={};blockedStamp_=lastStamp_;haveBlockedStamp_=haveStamp_;
        requireBegin_=phased_;context_=0;haveFrameTime_=false;
    }
    void context(uint32_t value) {
        if(value==context_) return;
        cancel();context_=value;
    }
    bool event(double stamp,double now,double x,double y,bool precise,
               uint32_t phase,uint32_t momentum,uint32_t target) {
        context(target);
        if(!target || !std::isfinite(stamp) || !std::isfinite(now) ||
           !std::isfinite(x) || !std::isfinite(y)) return false;
        bool began=(phase&0x21)!=0;
        if(!began && haveBlockedStamp_ && stamp==blockedStamp_) return false;
        if(requireBegin_ && !began && (phase || momentum)) return false;
        if(began || (!phase && !momentum)) requireBegin_=false;
        if(phase&0x10) {cancel();return false;}
        if(x==0 && y==0) return false;
        if(haveStamp_ && precise_!=precise) pending_={};
        haveBlockedStamp_=false;haveStamp_=true;lastStamp_=stamp;
        phased_=(phase||momentum);lastInput_=now;precise_=precise;
        double units=precise?precisePointsPerUnit:1.0;
        add(pending_.x,x/units);add(pending_.y,y/units);
        return true;
    }
    Delta frame(double now,uint32_t target) {
        context(target);
        if(!target || !std::isfinite(now)) return {};
        double dt=haveFrameTime_?now-frameTime_:1.0/120.0;
        haveFrameTime_=true;frameTime_=now;
        // Do not replay input accumulated before a long pause or time reset.
        if(dt<0 || dt>0.25) {pending_={};return {};}
        if(dt==0) return {};
        double alpha=precise_?-std::expm1(-dt/0.012):1.0;
        if(now-lastInput_>=0.10) alpha=1;
        Delta output{pending_.x*alpha,pending_.y*alpha};
        pending_.x-=output.x;pending_.y-=output.y;
        return output;
    }
private:
    Delta pending_{};
    double lastStamp_=0,blockedStamp_=0,frameTime_=0,lastInput_=0;
    uint32_t context_=0;
    bool haveStamp_=false,haveBlockedStamp_=false,requireBegin_=false,phased_=false;
    bool haveFrameTime_=false,precise_=false;
    static void add(double &pending,double value) {
        if(pending*value<0) pending=0; // a reversal should not fight an old tail
        pending+=value;
    }
};

// Untagged scrolling retains the hardware units/distance, then reconstructs
// discrete notches over a short interval. Precise input keeps its own path.
// If the first marked packet arrives after a hardware step was already
// delivered, finish that gesture on the old path instead of adding it twice.
class DesktopWheelRouter {
public:
    using Delta=DesktopScrollState::Delta;
    // AppKit can report a single ordinary wheel notch as a fractional line
    // (for example 0.1). Each nonzero packet must still select one row. Keep
    // integral line counts for coalesced packets, with a bounded magnitude.
    static double selectionUnits(double delta,double lines=0) {
        if(!std::isfinite(delta)||!std::isfinite(lines)||delta==0)return 0;
        return std::copysign(std::max(1.0,std::min(32.0,std::fabs(lines))),delta);
    }
    void cancel() {
        rawX_=rawY_=candidateX_=candidateY_=0;smooth_.cancel();coarse_.cancel();lastRawOutput_=-100;
        hardwareX_=hardwareY_=0;systemSelection_={};discrete_=false;
    }
    void hardware(float x,float y,double now) {
        if(!std::isfinite(now) || !std::isfinite(x) || !std::isfinite(y) || (x==0 && y==0)) return;
        // Keep the physical packet before Mos arbitration. Native interaction
        // selection compares a whole notch with its threshold in one frame.
        if(!hardwareX_&&!hardwareY_)hardwareTime_=now;
        hardwareX_+=x;hardwareY_+=y;
        finishWarmup(now);
        if(!mos_ || warming_) {addRaw(x,y,now);return;}
        if(now-candidateTime_>0.05) candidateX_=candidateY_=0;
        if(!candidateX_&&!candidateY_)candidateStart_=now;
        candidateX_+=x;candidateY_+=y;candidateTime_=now;
    }
    void system(double stamp,double now,double x,double y,bool precise,uint32_t phase,
                uint32_t momentum,uint32_t target,bool mosMarked,double linesX=0,double linesY=0) {
        if(!target||!std::isfinite(stamp)||!std::isfinite(now)||
           !std::isfinite(x)||!std::isfinite(y))return;
        // GCMouse's valueChanged callback is not an event stream: identical
        // successive notches may produce no callback. For ordinary F-list
        // input, only the deduplicated AppKit packets own selection distance.
        // Never fall back to a GC copy based on callback/frame ordering.
        selectionUsesSystem_=!mosMarked&&!precise&&!phase&&!momentum;
        if(selectionUsesSystem_&&target==1) {
            systemSelection_.x+=selectionUnits(x,linesX);
            systemSelection_.y+=selectionUnits(y,linesY);
        }
        if(!mosMarked) {
            bool nativePrecise=precise||phase||momentum;
            if(nativePrecise!=rawPrecise_)coarse_.cancel();
            rawPrecise_=nativePrecise;
            if(mos_) {
                smooth_.cancel();mos_=warming_=false;
                if(now-candidateTime_<=0.05) addRaw(candidateX_,candidateY_,candidateStart_);
                candidateX_=candidateY_=0;
            }
            return; // Hardware remains the distance/direction authority.
        }
        finishWarmup(now);
        if(!mos_) {
            mos_=true;warming_=now-lastRawOutput_<=0.06;
            if(!warming_) {rawX_=rawY_=0;coarse_.cancel();}
            else {
                auto remainder=coarse_.finish();
                addRaw(remainder.x,remainder.y,now);
            }
        }
        lastMos_=now;candidateX_=candidateY_=0;
        if(!warming_) smooth_.event(stamp,now,x,y,precise,phase,momentum,target);
    }
    bool hasPendingInput() const {
        return hardwareX_!=0||hardwareY_!=0||systemSelection_.x!=0||systemSelection_.y!=0;
    }
    Delta frame(double now,uint32_t target,uint64_t coarseTarget=0,bool discrete=false) {
        if(!target) {cancel();return {};}
        Delta hardware{hardwareX_,hardwareY_};hardwareX_=hardwareY_=0;
        Delta selection=systemSelection_;systemSelection_={};
        if(discrete || discrete_) {
            // Entering/leaving a selection list must not replay a zoom tail,
            // a Mos candidate, or a second copy of a physical notch.
            rawX_=rawY_=candidateX_=candidateY_=0;smooth_.cancel();coarse_.cancel();
            mos_=warming_=false;lastRawOutput_=-100;
            discrete_=discrete;
            if(discrete)return selectionUsesSystem_?selection:hardware;
            addRaw(hardware.x,hardware.y,hardwareTime_);
        }
        finishWarmup(now);
        if(mos_ && !warming_) {coarse_.cancel();return smooth_.frame(now,target);}
        Delta result{rawX_,rawY_};rawX_=rawY_=0;
        if(rawPrecise_||warming_){
            coarse_.cancel();if(result.x||result.y)lastRawOutput_=now;return result;
        }
        uint64_t scope=coarseTarget?coarseTarget:target;
        // Start the curve at input time, not the later engine frame.
        coarse_.event(std::min(now,rawTime_),result.x,result.y,scope);
        auto output=coarse_.frame(now,scope);
        if(output.x||output.y)lastRawOutput_=now;
        return {output.x,output.y};
    }
    bool usingMos() const {return mos_ && !warming_ && !discrete_;}
    void cancelCoarse() {
        coarse_.cancel();
        hardwareX_=hardwareY_=0;systemSelection_={};
        if(!mos_){rawX_=rawY_=0;}
    }
private:
    DesktopScrollState smooth_;
    DesktopCoarseScrollState coarse_;
    float rawX_=0,rawY_=0,candidateX_=0,candidateY_=0;
    float hardwareX_=0,hardwareY_=0;
    Delta systemSelection_{};
    bool selectionUsesSystem_=true;
    double hardwareTime_=0;
    double lastRawOutput_=-100,lastMos_=-100,candidateTime_=-100;
    double rawTime_=0,candidateStart_=0;
    void addRaw(double x,double y,double now){
        if(!x&&!y)return;
        if(!rawX_&&!rawY_)rawTime_=now;
        rawX_+=x;rawY_+=y;
    }
    bool mos_=false,warming_=false,rawPrecise_=false,discrete_=false;
    void finishWarmup(double now) {
        if(warming_ && now-lastMos_>0.20) {warming_=false;rawX_=rawY_=0;}
    }
};

// Deduplicate only nested dispatch of the same wheel packet. Independent Mos
// frames may legitimately reuse a timestamp and even identical deltas.
class DesktopScrollDispatchScope {
public:
    struct Key {
        double stamp=0,x=0,y=0;
        uint32_t phase=0,momentum=0;
        bool precise=false;
        bool operator==(const Key &b) const {
            return stamp==b.stamp && x==b.x && y==b.y && phase==b.phase &&
                momentum==b.momentum && precise==b.precise;
        }
    };
    explicit DesktopScrollDispatchScope(Key value):key(value),previous(active) {
        for(auto p=active;p;p=p->previous) if(p->key==key) first=false;
        active=this;
    }
    ~DesktopScrollDispatchScope() {active=previous;}
    bool first=true;
private:
    Key key;
    DesktopScrollDispatchScope *previous;
    inline static thread_local DesktopScrollDispatchScope *active=nullptr;
};
