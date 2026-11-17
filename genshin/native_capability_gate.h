#pragma once
#include <atomic>
#include <cstdint>
namespace NativeMotion {
// These parameters exist only in the isolated graph. Never report a virtual
// controller to the game or overwrite its global device-capability parameters.
class CapabilityGate {
    std::atomic<uint64_t> wanted{0};
    std::atomic<uint64_t> applied{UINT64_MAX};
public:
    static constexpr uint32_t motion=0xfd4d0071,dualSense=0xfd4d0072;
    struct Result {uint64_t epoch;bool active,changed;int motionStatus,dualSenseStatus;};
    void request(bool enabled){
        uint64_t old=wanted.load(std::memory_order_acquire);
        while(bool(old&1)!=enabled&&!wanted.compare_exchange_weak(old,((old>>1)+1)*2+enabled,std::memory_order_acq_rel)){}
    }
    uint64_t generation()const{return wanted.load(std::memory_order_acquire);}
    bool active()const {
        uint64_t next=generation();
        return (next&1)&&applied.load(std::memory_order_acquire)==next&&generation()==next;
    }
    // Called only from the main control timer, never the audio callback.
    template<class Writer> Result sync(Writer write){
        uint64_t next=generation();
        if(applied.load(std::memory_order_relaxed)==next)return {next,bool(next&1),false,1,1};
        float value=(next&1)?1.f:0.f;
        int a=write(motion,value),b=write(dualSense,value);
        bool ok=a==1&&b==1&&generation()==next;
        if(ok)applied.store(next,std::memory_order_release);
        return {next,ok&&bool(next&1),true,a,b};
    }
};
}
