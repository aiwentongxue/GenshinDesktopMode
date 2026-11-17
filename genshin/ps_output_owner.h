#pragma once
#include <atomic>

namespace PSAuthoredOutput {
enum class Backend:unsigned {None,USB,Bluetooth};
// The native PCM ring is single-consumer. A late CoreAudio callback must not
// drain it concurrently with the Bluetooth queue during a transport change.
class Owner {
    std::atomic<Backend> selected{Backend::None};
    std::atomic_flag busy=ATOMIC_FLAG_INIT;
public:
    void select(Backend b) {selected.store(b,std::memory_order_release);}
    Backend current()const {return selected.load(std::memory_order_acquire);}
    bool acquire(Backend b) {
        if(b==Backend::None||current()!=b||busy.test_and_set(std::memory_order_acquire))return false;
        if(current()==b)return true;
        busy.clear(std::memory_order_release);return false;
    }
    void release() {busy.clear(std::memory_order_release);}
};
class Lease {
    Owner &owner;bool held;
public:
    Lease(Owner &o,Backend b):owner(o),held(o.acquire(b)){}
    ~Lease(){if(held)owner.release();}
    explicit operator bool()const{return held;}
    Lease(const Lease &)=delete;Lease &operator=(const Lease &)=delete;
};
}
