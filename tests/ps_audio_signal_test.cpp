#include "../genshin/ps_audio_signal.h"
#include <cassert>
#include <limits>
#include <thread>
#include <iostream>

static double Energy(double rate,double frequency) {
    PSAudioTransform transform;transform.reset(rate,7);double sum=0;unsigned count=0;
    for(unsigned i=0;i<(unsigned)rate;++i)
        transform.sample(.4f*std::sin(i*2*3.141592653589793*frequency/rate),0,[&](PSAudioPair value){
            assert(value.generation==7&&value.right==0&&std::isfinite(value.left)&&std::abs(value.left)<=.200001f);
            sum+=value.left*value.left;++count;
        });
    assert(count==48000);return sum/count;
}
int main() {
    double bass=Energy(48000,110),treble=Energy(48000,5000);
    assert(bass>treble*40&&bass>0);
    Energy(44100,110);Energy(96000,110);Energy(8000,110);
    PSAudioTransform transform;transform.reset(48000,1);
    for(unsigned i=0;i<100;++i)transform.sample(std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),[](PSAudioPair p){assert(p.left==0&&p.right==0);});
    for(unsigned i=0;i<48000;++i)transform.sample(1,1,[](PSAudioPair p){assert(std::isfinite(p.left)&&std::abs(p.left)<=.200001f);});
    float last=1;for(unsigned i=0;i<48000;++i)transform.sample(0,0,[&](PSAudioPair p){last=p.left;});
    assert(std::abs(last)<.000001f);
    std::array<float,16> interleaved{};PSAudioOutputMap map;
    assert(map.add(interleaved.data(),4,interleaved.size()*4));map.write(2,{.12f,-.15f,1});
    for(unsigned i=0;i<16;++i)assert(interleaved[i]==(i==10?.12f:i==11?-.15f:0));
    float channels[4][8]{};map={};
    for(auto &c:channels)assert(map.add(c,1,sizeof(c)));
    map.write(7,{.1f,.2f,1});assert(channels[0][7]==0&&channels[1][7]==0&&channels[2][7]==.1f&&channels[3][7]==.2f);
    assert(!map.add(channels[0],1,sizeof(channels[0])));
    map={};assert(!map.add(nullptr,4,16));assert(!map.add(channels[0],4,15));
    PSAudioRing ring;
    assert(ring.push({.1f,.2f,1}));assert(ring.push({.3f,.4f,2}));PSAudioPair value;
    assert(ring.pop(value,2)&&value.left==.3f);assert(!ring.pop(value,2)&&value.left==0);
    for(unsigned i=0;i<PSAudioRing::capacity;++i)assert(ring.push({float(i),0,3}));
    assert(!ring.push({}));assert(ring.pop(value,3)&&value.left==PSAudioRing::capacity-1536);
    ring.discard();assert(!ring.pop(value,3));
    PSAudioRing threaded;std::atomic<bool> done{false};
    std::thread producer([&]{for(unsigned i=1;i<=200000;++i)while(!threaded.push({float(i),-float(i),9}))std::this_thread::yield();done=true;});
    float previous=0;
    while(!done||threaded.read.load()!=threaded.written.load()) {
        PSAudioPair p;if(!threaded.pop(p,9))continue;
        assert(p.left>previous&&p.right==-p.left);previous=p.left;
    }
    producer.join();assert(previous==200000);
    std::cout<<"PS audio: band filtering, 44.1/48/96/8 kHz resampling, bounded samples, silence, speaker-safe channel mapping, stale-frame drop and concurrent ring passed; no hardware output\n";
}
