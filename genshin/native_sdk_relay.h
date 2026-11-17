#pragma once
#include <atomic>
#include <cstdint>

namespace NativeMotion {
// Exact final C SDK ABI, independently recovered from both installed clients.
// The original owns its callback cookie, external-source array and return ID.
// The private graph contains no external sources; never retain their pointers.
// A PC-only Motion event may have no playable counterpart in the iOS bank.
// Preserve the original return ID, while allowing the independent fixed graph
// to match that event even if ordinary playback failed or used external media.
struct SDKRelay {
    using Post=uint32_t (*)(uint32_t,uint64_t,uint32_t,void *,uint32_t,void *,uint32_t,bool,uint32_t,uint64_t);
    using Stop=void (*)(uint32_t,int,int);
    using Observe=void (*)(uint32_t,uint64_t,uint32_t,uint32_t,uint64_t);
    using ObserveStop=void (*)(uint32_t,int,int);
    std::atomic<Post> originalPost{nullptr};std::atomic<Stop> originalStop{nullptr};
    std::atomic<Observe> observe{nullptr};std::atomic<ObserveStop> observeStop{nullptr};
    std::atomic<uint64_t> calls{0},externalSkipped{0},failedSkipped{0}; // Legacy names; counts observed calls, no exclusions.
    uint32_t post(uint32_t event,uint64_t owner,uint32_t flags,void *cookie,
                  uint32_t count,void *external,uint32_t requested,bool deferred,
                  uint32_t customType,uint64_t customValue){
        // Preserve the game's call exactly, even when private dispatch is off.
        uint32_t playing=originalPost.load(std::memory_order_acquire)(event,owner,flags,cookie,count,external,
                                      requested,deferred,customType,customValue);
        ++calls;
        if(count)++externalSkipped;
        if(!playing)++failedSkipped;
        if(auto observer=observe.load(std::memory_order_acquire))observer(event,owner,playing,customType,customValue);
        return playing;
    }
    void stop(uint32_t playing,int transition,int curve){
        originalStop.load(std::memory_order_acquire)(playing,transition,curve);
        if(auto observer=observeStop.load(std::memory_order_acquire))if(playing)observer(playing,transition,curve);
    }
};
}
