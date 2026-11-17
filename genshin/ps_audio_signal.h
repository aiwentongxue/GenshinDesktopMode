#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>

struct PSAudioPair {float left=0,right=0;uint64_t generation=0;};

// One game render producer and one controller-device consumer. Neither audio
// thread allocates, locks, waits, calls Objective-C or edits game audio data.
struct PSAudioRing {
    static constexpr uint64_t capacity=8192;
    std::array<PSAudioPair,capacity> samples{};
    alignas(64) std::atomic<uint64_t> written{0};
    alignas(64) std::atomic<uint64_t> read{0};
    bool push(PSAudioPair sample) {
        uint64_t w=written.load(std::memory_order_relaxed);
        if(w-read.load(std::memory_order_acquire)>=capacity)return false;
        samples[w%capacity]=sample;written.store(w+1,std::memory_order_release);return true;
    }
    void discard() {read.store(written.load(std::memory_order_acquire),std::memory_order_release);}
    bool pop(PSAudioPair &value,uint64_t generation) {
        uint64_t r=read.load(std::memory_order_relaxed),w=written.load(std::memory_order_acquire);
        // Keep at most 32 ms of queued audio. A delayed output never replays a
        // long combat backlog after focus changes, underruns or USB stalls.
        if(w-r>1536)r=w-1536;
        while(r<w) {
            value=samples[r%capacity];++r;
            if(value.generation==generation) {read.store(r,std::memory_order_release);return true;}
        }
        read.store(r,std::memory_order_release);value={};return false;
    }
};

struct PSAudioTransform {
    struct Channel {double previous=0,high=0,low=0,fast=0,slow=0;};
    Channel channels[2];
    double rate=0,highCoefficient=0,lowCoefficient=0,fastCoefficient=0,slowCoefficient=0,phase=0;
    float previous[2]{};
    uint64_t generation=0;
    void reset(double inputRate,uint64_t epoch) {
        *this={};rate=inputRate;generation=epoch;
        constexpr double pi=3.141592653589793;
        highCoefficient=std::exp(-2*pi*35/rate);
        lowCoefficient=1-std::exp(-2*pi*260/rate);
        fastCoefficient=1-std::exp(-1/(.005*rate));
        slowCoefficient=1-std::exp(-1/(.20*rate));
    }
    float filter(float value,unsigned side) {
        if(!std::isfinite(value))value=0;
        value=std::clamp(value,-1.f,1.f);auto &c=channels[side];
        c.high=highCoefficient*(c.high+value-c.previous);c.previous=value;
        c.low+=lowCoefficient*(c.high-c.low);
        double amplitude=std::abs(c.low);
        c.fast+=fastCoefficient*(amplitude-c.fast);
        c.slow+=slowCoefficient*(amplitude-c.slow);
        // Emphasize changes in game sound over a constant musical/ambient
        // bass. This is an audio-derived effect, not authored PS5 feedback.
        double transient=std::clamp((c.fast-c.slow*.90)/(c.fast+.0001),0.,1.);
        return (float)std::clamp(c.low*1.8*transient,-.20,.20);
    }
    template<class Emit> void sample(float left,float right,Emit emit) {
        if(!std::isfinite(rate)||rate<8000||rate>192000)return;
        float current[]={filter(left,0),filter(right,1)};
        phase+=48000;
        while(phase>=rate) {
            double fraction=1-(phase-rate)/48000;
            emit(PSAudioPair{(float)(previous[0]+(current[0]-previous[0])*fraction),
                (float)(previous[1]+(current[1]-previous[1])*fraction),generation});
            phase-=rate;
        }
        previous[0]=current[0];previous[1]=current[1];
    }
};

// USB DualSense's first two channels are speaker/headset audio. This module
// writes only the last two actuator channels, leaving its own first two zero.
struct PSAudioOutputMap {
    struct Channel {float *data=nullptr;unsigned stride=0;};
    Channel channels[4];unsigned count=0,frames=0;
    bool add(float *data,unsigned channelCount,unsigned byteCount) {
        if(!data||!channelCount||channelCount>4||count+channelCount>4||
           byteCount%(4*channelCount))return false;
        unsigned length=byteCount/(4*channelCount);
        if(!length||(count&&frames!=length))return false;
        frames=length;
        for(unsigned i=0;i<channelCount;++i)channels[count++]={data+i,channelCount};
        return true;
    }
    void write(unsigned frame,PSAudioPair value) {
        if(count!=4||frame>=frames)return;
        channels[2].data[frame*channels[2].stride]=value.left;
        channels[3].data[frame*channels[3].stride]=value.right;
    }
};
