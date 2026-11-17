#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace NativeTrigger {
// Original ScePad units, not Apple's normalized positions/frequencies.
enum class Mode:uint8_t {Off,Feedback,Weapon,Vibration};
struct Effect {
    Mode mode=Mode::Off;
    uint8_t start=0,end=0,strength=0,frequency=0;
    bool valid()const {
        if(mode==Mode::Off)return true;
        if(mode==Mode::Feedback)return start<=9&&strength<=8;
        if(mode==Mode::Weapon)return start>=2&&end<=8&&start<end&&strength<=8;
        return mode==Mode::Vibration&&start<=9&&strength<=8;
    }
    bool operator==(const Effect &b)const {
        return mode==b.mode&&start==b.start&&end==b.end&&strength==b.strength&&frequency==b.frequency;
    }
    bool operator!=(const Effect &b)const{return !(*this==b);}
};
using Pair=std::array<Effect,2>;
// The PC chain truncates its evaluated Single parameters to bytes, then the
// ScePad API validates the zones/strength. Reject malformed values before a
// float-to-integer conversion instead of letting an invalid command stick.
inline bool Parameter(float value,uint8_t &out) {
    if(!std::isfinite(value)||value<0||value>=256)return false;
    out=uint8_t(value);return true;
}
inline bool FromEvaluated(Mode mode,float start,float parameter0,float parameter1,Effect &out) {
    out={};Effect value;value.mode=mode;
    if(mode==Mode::Off)return true;
    if(!Parameter(start,value.start))return false;
    if(mode==Mode::Feedback) {
        if(!Parameter(parameter0,value.strength))return false;
    } else if(mode==Mode::Weapon) {
        if(!Parameter(parameter0,value.end)||!Parameter(parameter1,value.strength))return false;
    } else if(mode==Mode::Vibration) {
        if(!Parameter(parameter0,value.frequency)||!Parameter(parameter1,value.strength))return false;
    } else return false;
    if(!value.valid())return false;out=value;return true;
}
inline std::array<uint8_t,11> Encode(const Effect &value) {
    std::array<uint8_t,11> result{};result[0]=5;
    if(!value.valid()||value.mode==Mode::Off||!value.strength||
       (value.mode==Mode::Vibration&&!value.frequency))return result;
    uint16_t zones=0;uint64_t strengths=0;
    if(value.mode==Mode::Weapon) {
        result[0]=0x25;zones=(1u<<value.start)|(1u<<value.end);
        result[3]=value.strength-1;
    } else {
        result[0]=value.mode==Mode::Feedback?0x21:0x26;
        for(unsigned i=value.start;i<10;++i) {
            zones|=1u<<i;strengths|=uint64_t(value.strength-1)<<(3*i);
        }
        for(unsigned i=0;i<6;++i)result[3+i]=uint8_t(strengths>>(8*i));
        if(value.mode==Mode::Vibration)result[9]=value.frequency;
    }
    result[1]=uint8_t(zones);result[2]=uint8_t(zones>>8);return result;
}
}
