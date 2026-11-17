#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// InputManager's seven serialized strings precede gravity/dead/sensitivity,
// snap/invert and the native axis kind/index/device. Validate the complete
// record before using a setting; asset strings alone are not sufficient.
namespace CharacterMouseUnits {
static bool sensitivity(const uint8_t *data,size_t length,const char *name,int axis,float &result) {
    const size_t wanted=std::strlen(name);
    for(size_t start=0;start+4+wanted<=length;++start) {
        uint32_t n;std::memcpy(&n,data+start,4);
        if(n!=wanted||std::memcmp(data+start+4,name,wanted)) continue;
        size_t p=start;bool valid=true;
        for(unsigned i=0;i<7;++i) {
            if(p>length||length-p<4) {valid=false;break;}
            std::memcpy(&n,data+p,4);p+=4;
            size_t padded=(size_t(n)+3)&~size_t(3);
            if(n>4096||p>length||padded>length-p) {valid=false;break;}
            p+=padded;
        }
        if(!valid||p>length||length-p<28) continue;
        float gravity,dead,gain;int32_t kind,index,device;
        std::memcpy(&gravity,data+p,4);std::memcpy(&dead,data+p+4,4);std::memcpy(&gain,data+p+8,4);
        std::memcpy(&kind,data+p+16,4);std::memcpy(&index,data+p+20,4);std::memcpy(&device,data+p+24,4);
        if(!std::isfinite(gravity)||gravity<0||!std::isfinite(dead)||dead<0||
           !std::isfinite(gain)||gain<=0||gain>100||data[p+12]>1||data[p+13]>1||
           kind!=1||index!=axis||device<0||device>16) continue;
        result=gain;return true;
    }
    return false;
}
static float fromPixels(float pixels,double logicalSize,int renderSize,float gain) {
    if(!std::isfinite(pixels)||!std::isfinite(logicalSize)||logicalSize<=0||renderSize<=0||
       !std::isfinite(gain)||gain<=0) return 0;
    return (float)(pixels*logicalSize/renderSize*gain);
}
}
