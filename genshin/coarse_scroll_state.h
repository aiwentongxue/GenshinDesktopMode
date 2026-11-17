#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Distance-conserving finite reconstruction of coarse wheel notches. Each
// notch decelerates continuously to zero; there is no terminal remainder dump.
// Mos/trackpad precise streams continue to bypass this filter.
class DesktopCoarseScrollState {
public:
    struct Delta {double x=0,y=0;};
    static constexpr double maximumTail=.12;
    void cancel(){count_=0;context_=0;haveFrame_=haveInput_=false;}
    void context(uint64_t value){if(value!=context_){cancel();context_=value;}}
    void event(double now,double x,double y,uint64_t target){
        context(target);
        if(!target||!std::isfinite(now)||!std::isfinite(x)||!std::isfinite(y)||(!x&&!y))return;
        if(haveInput_&&(now<lastInput_||now-lastInput_>.25))count_=0;
        for(unsigned i=0;i<count_;++i){
            if((x>0&&parts_[i].amount.x<0)||(x<0&&parts_[i].amount.x>0))parts_[i].amount.x=0;
            if((y>0&&parts_[i].amount.y<0)||(y<0&&parts_[i].amount.y>0))parts_[i].amount.y=0;
        }
        // Bounded storage, no allocation on the event path. Extreme bursts
        // coalesce the newest unsent remainder, preserving its total distance.
        if(count_==parts_.size()){
            Part &last=parts_[--count_];
            x+=last.amount.x*(1-last.delivered);y+=last.amount.y*(1-last.delivered);
        }
        parts_[count_++]={now,0,{x,y}};
        lastInput_=now;haveInput_=true;
    }
    Delta frame(double now,uint64_t target){
        context(target);
        if(!target||!std::isfinite(now)){cancel();return {};}
        if(haveFrame_&&(now<lastFrame_||now-lastFrame_>.25)){
            // A new event after a pause is usable; old inertial tails are not.
            unsigned keep=0;
            for(unsigned i=0;i<count_;++i)if(now>=parts_[i].start&&now-parts_[i].start<=1./30.)parts_[keep++]=parts_[i];
            count_=keep;
        }
        haveFrame_=true;lastFrame_=now;Delta out{};unsigned keep=0;
        for(unsigned i=0;i<count_;++i){
            Part part=parts_[i];double t=std::clamp((now-part.start)/maximumTail,0.,1.);
            double remaining=1-t,cdf=1-remaining*remaining*remaining;
            double fraction=std::max(0.,cdf-part.delivered);
            out.x+=part.amount.x*fraction;out.y+=part.amount.y*fraction;
            part.delivered=std::max(part.delivered,cdf);
            if(part.delivered<1&&(part.amount.x||part.amount.y))parts_[keep++]=part;
        }
        count_=keep;return out;
    }
    bool pending()const{return count_!=0;}
    Delta finish(){
        Delta out{};for(unsigned i=0;i<count_;++i){
            out.x+=parts_[i].amount.x*(1-parts_[i].delivered);
            out.y+=parts_[i].amount.y*(1-parts_[i].delivered);
        }
        cancel();return out;
    }
private:
    struct Part{double start=0,delivered=0;Delta amount{};};
    std::array<Part,64> parts_{};
    unsigned count_=0;
    double lastFrame_=0,lastInput_=0;
    uint64_t context_=0;
    bool haveFrame_=false,haveInput_=false;
};
