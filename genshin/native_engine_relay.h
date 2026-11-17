#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
namespace NativeMotion {
// All three native PostEvent overloads converge on this seven-argument API.
// Its stack-owned custom block is copied by the original engine; only immutable
// IDs are observed here, with the original callback, cookie and return intact.
struct EngineRelay {
    using Post=uint32_t (*)(uint32_t,uint64_t,uint32_t,void *,void *,void *,uint32_t);
    using Observe=void (*)(uint32_t,uint64_t,uint32_t,uint32_t,uint64_t);
    using Attempt=void (*)(uint32_t,uint32_t,uint32_t);
    struct Custom {void *external,*reserved;uint32_t extra,type;uint64_t value;};
    static_assert(sizeof(Custom)==32);
    static inline thread_local unsigned privateDepth=0;
    struct PrivateScope {PrivateScope(){++privateDepth;}~PrivateScope(){--privateDepth;}};
    std::atomic<Post> original{nullptr};std::atomic<Observe> observe{nullptr};std::atomic<Attempt> attempt{nullptr};
    std::atomic<uint64_t> calls{0},privateCalls{0},failed{0},externalCalls{0};
    uint32_t post(uint32_t event,uint64_t owner,uint32_t flags,void *callback,void *cookie,void *params,uint32_t requested){
        Custom meta{};if(params)memcpy(&meta,params,sizeof(meta));
        uint32_t playing=original.load(std::memory_order_acquire)(event,owner,flags,callback,cookie,params,requested);
        if(privateDepth){++privateCalls;return playing;}
        ++calls;if(!playing)++failed;if(meta.external)++externalCalls;
        if(auto log=attempt.load(std::memory_order_acquire))log(event,playing,meta.external?1:0);
        if(auto sink=observe.load(std::memory_order_acquire))sink(event,owner,playing,meta.type,meta.value);
        return playing;
    }
};
}
