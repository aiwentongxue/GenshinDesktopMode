#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace NativeMotion {
struct Mapping {uint32_t event,privateEvent;};
enum class Operation:uint8_t {Play,StopOriginal};
struct Command {
    uint64_t epoch=0,owner=0,customValue=0;
    uint32_t event=0,privateEvent=0,originalPlaying=0,customType=0;
    int transition=0,curve=4;Operation operation=Operation::Play;
};
template<size_t N> class Queue {
    struct Slot {std::atomic<uint64_t> sequence;Command command;};
    std::array<Slot,N> slots;std::atomic<uint64_t> write{0};uint64_t read=0;
public:
    Queue(){for(uint64_t i=0;i<N;++i)slots[i].sequence=i;}
    bool push(const Command &command){
        uint64_t pos=write.load(std::memory_order_relaxed);
        for(unsigned attempt=0;attempt<64;++attempt){
            auto &slot=slots[pos%N];auto seq=slot.sequence.load(std::memory_order_acquire);
            int64_t diff=(int64_t)(seq-pos);
            if(!diff){if(!write.compare_exchange_weak(pos,pos+1,std::memory_order_relaxed))continue;
                slot.command=command;slot.sequence.store(pos+1,std::memory_order_release);return true;}
            if(diff<0)return false;pos=write.load(std::memory_order_relaxed);
        }return false;
    }
    bool pop(Command &command){
        auto &slot=slots[read%N];if(slot.sequence.load(std::memory_order_acquire)!=read+1)return false;
        command=slot.command;slot.sequence.store(read+N,std::memory_order_release);++read;return true;
    }
};

// Playback API calls occur only in drain(), on one native control thread.
// Game/input observers enqueue immutable IDs and never run audio work.
template<size_t Voices=1024,size_t Commands=512> class Dispatcher {
public:
    struct Token {
        std::atomic<unsigned> state{0}; // 0 free, 1 posting, 2 playing
        std::atomic<uint32_t> playing{0};uint32_t original=0;uint64_t epoch=0;
    };
    struct API {
        void *context=nullptr;
        uint32_t (*post)(void *,const Command &,Token *)=nullptr;
        void (*stop)(void *,uint32_t,int,int)=nullptr;
    };
private:
    const Mapping *mapping;size_t count;API api;
    std::array<Token,Voices> tokens;
    // A native stop can race the SDK wrapper's return. Remember bounded stop
    // IDs within one generation so a late mirror cannot restart that effect.
    std::array<uint32_t,Voices> stoppedOriginals{};size_t stoppedCount=0;
    Queue<Commands> queue;std::atomic<uint64_t> epoch{0},drops{0};uint64_t observed=0;
    void nextEpoch(bool enabled){
        uint64_t old=epoch.load();
        while(!epoch.compare_exchange_weak(old,((old>>1)+1)*2+enabled,std::memory_order_acq_rel)){}
    }
    void overflow(){++drops;uint64_t old=epoch.load();
        while(!epoch.compare_exchange_weak(old,((old>>1)+1)*2+(old&1),std::memory_order_acq_rel)){}
    }
    void stopToken(Token &token,int transition=0,int curve=4){
        if(token.state.load(std::memory_order_acquire)==2){
            uint32_t id=token.playing.load();if(id&&api.stop)api.stop(api.context,id,transition,curve);
        }
        // Keep the token reserved until its native end callback. Reusing it
        // early would let an old callback release a new effect's ownership.
    }
public:
    Dispatcher(const Mapping *table,size_t n,API a):mapping(table),count(n),api(a){}
    void setEnabled(bool enabled){if(bool(epoch.load()&1)!=enabled)nextEpoch(enabled);}
    uint64_t generation()const{return epoch.load(std::memory_order_acquire);}
    uint64_t dropped()const{return drops.load();}
    size_t active()const{size_t n=0;for(auto &t:tokens)n+=t.state.load(std::memory_order_acquire)!=0;return n;}
    bool submit(uint32_t event,uint64_t owner,uint32_t originalPlaying,uint32_t customType=0,uint64_t customValue=0){
        uint64_t state=epoch.load(std::memory_order_acquire);if(!(state&1)||!event)return false;
        size_t a=0,b=count;while(a<b){size_t m=(a+b)/2;if(mapping[m].event<event)a=m+1;else b=m;}
        if(a==count||mapping[a].event!=event)return false;
        Command c;c.epoch=state;c.owner=owner;c.event=event;c.privateEvent=mapping[a].privateEvent;
        c.originalPlaying=originalPlaying;c.customType=customType;c.customValue=customValue;
        if(queue.push(c))return true;overflow();return false;
    }
    bool stopOriginal(uint32_t id,int transition=0,int curve=4){
        if(!id)return false;Command c;c.operation=Operation::StopOriginal;c.originalPlaying=id;
        c.transition=transition;c.curve=curve;c.epoch=epoch.load();
        if(queue.push(c))return true;overflow();return false;
    }
    static void ended(Token *token){if(token)token->state.store(0,std::memory_order_release);}
    void drain(){
        uint64_t state=epoch.load(std::memory_order_acquire);
        if(state!=observed){for(auto &token:tokens)stopToken(token);observed=state;stoppedCount=0;}
        Command command;unsigned budget=64;
        while(budget--&&queue.pop(command)){
            if(command.epoch!=state||!(state&1))continue;
            if(command.operation==Operation::StopOriginal){
                bool known=false;for(size_t i=0;i<stoppedCount;++i)known|=stoppedOriginals[i]==command.originalPlaying;
                if(!known){
                    if(stoppedCount==stoppedOriginals.size()){overflow();break;}
                    stoppedOriginals[stoppedCount++]=command.originalPlaying;
                }
                for(auto &token:tokens)if(token.state.load()&&token.epoch==state&&token.original==command.originalPlaying)
                    stopToken(token,command.transition,command.curve);
                continue;
            }
            bool stopped=false;for(size_t i=0;i<stoppedCount;++i)stopped|=stoppedOriginals[i]==command.originalPlaying;
            if(stopped)continue;
            Token *token=nullptr;
            for(auto &candidate:tokens){unsigned free=0;
                if(candidate.state.compare_exchange_strong(free,1,std::memory_order_acq_rel)){token=&candidate;break;}}
            if(!token){overflow();break;}
            token->original=command.originalPlaying;token->epoch=state;token->playing=0;
            uint32_t id=api.post?api.post(api.context,command,token):0;token->playing=id;
            if(!id)token->state=0;
            else{unsigned pending=1;token->state.compare_exchange_strong(pending,2,std::memory_order_acq_rel);}
        }
    }
};
}
