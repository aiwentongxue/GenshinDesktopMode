#pragma once
#include "ps_audio_signal.h"
namespace NativeMotion {
// Direct native PCM path. No sound-derived filtering, transient emphasis,
// sensitivity adjustment or resampling is applied to authored haptic samples.
class Signal {
    struct Ring {
        static constexpr size_t capacity=8192;
        std::array<PSAudioPair,capacity> samples{};
        alignas(64) std::atomic<uint64_t> written{0};
        alignas(64) std::atomic<uint64_t> consumed{0};
        bool push(PSAudioPair value){
            uint64_t w=written.load(std::memory_order_relaxed);
            if(w-consumed.load(std::memory_order_acquire)>=capacity)return false;
            samples[w%capacity]=value;written.store(w+1,std::memory_order_release);return true;
        }
        void discard(){consumed.store(written.load(std::memory_order_acquire),std::memory_order_release);}
        bool pop(PSAudioPair &value,uint64_t generation){
            uint64_t r=consumed.load(std::memory_order_relaxed),w=written.load(std::memory_order_acquire);
            while(r<w){value=samples[r++%capacity];if(value.generation==generation){consumed.store(r,std::memory_order_release);return true;}}
            consumed.store(r,std::memory_order_release);value={};return false;
        }
    } ring;
    std::atomic<uint64_t> epoch{0},dropped{0};
public:
    void setEnabled(bool enabled){
        uint64_t old=epoch.load();if(bool(old&1)==enabled)return;
        while(!epoch.compare_exchange_weak(old,((old>>1)+1)*2+enabled,std::memory_order_acq_rel)){}
    }
    uint64_t generation()const{return epoch.load();}
    uint64_t overflows()const{return dropped.load(std::memory_order_relaxed);}
    // A transport discontinuity must retire queued audio without changing the
    // requested enable state or touching the consumer's ring index.
    void retireBuffered(){epoch.fetch_add(2,std::memory_order_acq_rel);}
    bool read(PSAudioPair &p){
        p={};uint64_t state=epoch.load(std::memory_order_acquire);bool result=false;
        if(state&1)result=ring.pop(p,state);else ring.discard();
        if(epoch.load(std::memory_order_acquire)!=state){p={};result=false;}
        return result;
    }
    bool push(const float *planar,unsigned maximum,unsigned valid,unsigned channels,unsigned rate,float firstGain,float lastGain){
        uint64_t state=epoch.load(std::memory_order_acquire);
        if(!(state&1))return false;
        if(!planar||!maximum||maximum>4096||valid>maximum||channels!=2||rate!=48000||
           !std::isfinite(firstGain)||!std::isfinite(lastGain))return false;
        for(unsigned ch=0;ch<2;++ch)for(unsigned i=0;i<valid;++i)
            if(!std::isfinite(planar[ch*maximum+i]))return false;
        bool complete=true;
        for(unsigned i=0;i<valid;++i){
            double gain=double(firstGain)+(double(lastGain)-firstGain)*double(i)/maximum;
            PSAudioPair p{(float)std::clamp(planar[i]*gain,-1.,1.),
                          (float)std::clamp(planar[maximum+i]*gain,-1.,1.),state};
            if(!ring.push(p)){
                ++dropped;complete=false;
                // Retire this complete waveform generation rather than
                // splicing an old backlog into an authored continuous effect.
                uint64_t wanted=state;epoch.compare_exchange_strong(wanted,((state>>1)+1)*2,std::memory_order_acq_rel);
                break;
            }
        }return complete;
    }
    void render(float *interleaved,unsigned frames){
        if(!interleaved||frames>16384)return;
        // Only this consumer modifies the queue read pointer. Control threads
        // cancel by generation, so they cannot race it backwards with discard.
        for(unsigned i=0;i<frames;++i){
            PSAudioPair p;read(p);
            interleaved[i*4]=0;interleaved[i*4+1]=0;
            interleaved[i*4+2]=p.left;interleaved[i*4+3]=p.right;
        }
    }
};
}
