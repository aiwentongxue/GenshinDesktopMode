#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace NativeMotionSource {
// Motion Source 0x01990002. The PC renderer fills each planar channel with
// the selected internal driver value. It is a control source, not a sine
// generator. RTPC/time/state modulation is performed by the original engine.
// Reverse-checked against the read-only PC Mmoron.dll factories 0x40b80/
// 0x40bc0, render 0x40c00, parameter update 0x40e00 and bank reader 0x40fa0.
struct State {
    std::array<float,8> drivers{};
    std::array<uint16_t,8> assignments{};
    uint16_t count=0;
    static bool mapping(const void *data,uint32_t bytes,std::array<uint16_t,8> &values,uint16_t &count) {
        if(!data||bytes<2)return false;
        uint16_t n=0;memcpy(&n,data,2);
        if(!n||n>values.size()||bytes!=2u+2u*n)return false;
        std::array<uint16_t,8> next{};
        for(unsigned i=0;i<n;++i){memcpy(&next[i],(const char *)data+2+2*i,2);if(next[i]>=8)return false;}
        values=next;count=n;return true;
    }
    bool block(const void *data,uint32_t bytes) {
        if(!data||bytes<36||bytes>50)return false;
        State next;memcpy(next.drivers.data(),data,32);
        for(float v:next.drivers)if(!std::isfinite(v)||v<0||v>1)return false;
        if(!mapping((const char *)data+32,bytes-32,next.assignments,next.count))return false;
        *this=next;return true;
    }
    bool parameter(int16_t id,const void *data,uint32_t bytes) {
        if(id==0x7fff)return mapping(data,bytes,assignments,count);
        if(id<1||id>8||!data||bytes!=sizeof(float))return false;
        float value;memcpy(&value,data,4);if(!std::isfinite(value))return false;
        drivers[id-1]=std::clamp(value,0.f,1.f);return true;
    }
    bool render(float *planar,unsigned channels,unsigned maximum)const {
        if(!planar||!maximum||maximum>4096||channels!=count||!count||count>8)return false;
        for(unsigned ch=0;ch<count;++ch)
            std::fill_n(planar+ch*maximum,maximum,drivers[assignments[ch]]);
        return true;
    }
};
}
