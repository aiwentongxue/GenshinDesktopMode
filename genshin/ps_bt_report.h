#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>

// Independently implemented wire format, checked against SAxense (Sdore,
// 2025): https://github.com/egormanga/SAxense/blob/master/SAxense.c
// Bluetooth report 0x32 carries 32 signed stereo frames at 3 kHz. This lane
// carries authored grip audio; it is not the 0x31 rumble/trigger/LED report.
namespace PSBluetooth {
constexpr size_t reportBytes=142,framesPerPacket=32,sampleBytes=64;
constexpr unsigned sourceRate=48000,wireRate=3000,decimation=16;
constexpr uint64_t periodNanos=uint64_t(framesPerPacket)*1000000000/wireRate;
using Samples=std::array<int8_t,sampleBytes>;
using Report=std::array<uint8_t,reportBytes>;

inline uint32_t Checksum(const uint8_t *data,size_t count) {
    uint32_t crc=0xffffffffu;
    auto byte=[&](uint8_t b) {
        crc^=b;
        for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));
    };
    byte(0xa2);for(size_t i=0;i<count;++i)byte(data[i]);return ~crc;
}
inline Report MakeReport(const Samples &pcm,uint8_t counter) {
    Report r{};
    r[0]=0x32;r[1]=0;
    r[2]=0x91;r[3]=7;r[4]=0xfe;r[9]=0xff;r[10]=counter;
    r[11]=0x92;r[12]=sampleBytes;
    for(size_t i=0;i<pcm.size();++i)r[13+i]=static_cast<uint8_t>(pcm[i]);
    uint32_t crc=Checksum(r.data(),r.size()-4);
    for(unsigned i=0;i<4;++i)r[r.size()-4+i]=uint8_t(crc>>(8*i));
    return r;
}

// A decimator needs an anti-alias filter: taking every 16th float would turn
// high-frequency content into an unrelated low-frequency grip waveform.
// 321-tap Blackman FIR, unity DC, 1.2 kHz cutoff; 3.33 ms group delay. Both
// channels share exactly the same timing and gain. No extra rumble shaping.
class Decimator {
    static constexpr size_t taps=321;
    std::array<double,taps> coefficients{};
    std::array<float,taps> left{},right{};
    size_t head=0;unsigned phase=0;
    static int8_t quantize(double value) {
        if(!std::isfinite(value))return 0;
        return int8_t(std::lround(std::clamp(value,-1.,1.)*127.));
    }
public:
    Decimator() {
        constexpr double pi=3.14159265358979323846,fc=1200./sourceRate;
        double sum=0;
        for(size_t i=0;i<taps;++i) {
            double n=double(i)-double(taps-1)/2;
            double sinc=n==0?2*fc:std::sin(2*pi*fc*n)/(pi*n);
            double w=.42-.5*std::cos(2*pi*i/(taps-1))+.08*std::cos(4*pi*i/(taps-1));
            coefficients[i]=sinc*w;sum+=coefficients[i];
        }
        for(double &c:coefficients)c/=sum;
    }
    void reset() {left.fill(0);right.fill(0);head=0;phase=0;}
    bool push(float l,float r,int8_t &outLeft,int8_t &outRight) {
        left[head]=std::isfinite(l)?std::clamp(l,-1.f,1.f):0;
        right[head]=std::isfinite(r)?std::clamp(r,-1.f,1.f):0;
        head=(head+1)%taps;
        if(++phase<decimation)return false;
        phase=0;double a=0,b=0;size_t index=head;
        for(size_t i=0;i<taps;++i) {
            index=index?index-1:taps-1;
            a+=coefficients[i]*left[index];b+=coefficients[i]*right[index];
        }
        outLeft=quantize(a);outRight=quantize(b);return true;
    }
};
}
