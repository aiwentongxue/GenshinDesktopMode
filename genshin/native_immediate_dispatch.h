#pragma once
#include "native_dispatch.h"
namespace NativeMotion {
// Post into the engine's own command queue before the caller can change or
// unregister its emitter. The timer only retires canceled voices; it never
// starts a delayed copy of a game event.
template<size_t Voices=1024,size_t Stops=8192> class ImmediateDispatcher {
public:
    enum State:unsigned {Free,Preparing,Posting,Playing,EarlyEnd,Pinned,PinnedEnd};
    struct Token {
        std::atomic<unsigned> state{Free};
        uint32_t playing=0,original=0;uint64_t epoch=0;
        bool stopIssued=false;uint64_t cancelEpoch=0;
    };
    struct API {
        void *context=nullptr;
        uint32_t (*post)(void *,const Command &,Token *)=nullptr;
        void (*stop)(void *,uint32_t,int,int)=nullptr;
    };
    struct Pending {
        ImmediateDispatcher *owner=nullptr;uint64_t previous=UINT64_MAX;
        Pending(ImmediateDispatcher *o,uint64_t e):owner(o){
            if(owner){++owner->pendingGames;previous=scopeEpoch;scopeEpoch=e;}
        }
        Pending(const Pending &)=delete;Pending &operator=(const Pending &)=delete;
        ~Pending(){if(owner){scopeEpoch=previous;--owner->pendingGames;}}
    };
private:
    const Mapping *mapping;size_t count;API api;
    std::array<Token,Voices> tokens;
    struct StopRecord {std::atomic<uint64_t> key{0},parameters{0};};
    std::array<StopRecord,Stops> stopped{};
    std::atomic<uint64_t> epoch{0},drops{0};
    std::atomic<unsigned> pendingGames{0};
    static inline thread_local uint64_t scopeEpoch=UINT64_MAX;
    bool mapped(uint32_t event)const{
        size_t a=0,b=count;while(a<b){size_t m=(a+b)/2;if(mapping[m].event<event)a=m+1;else b=m;}
        return a<count&&mapping[a].event==event;
    }
    void overflow(){++drops;uint64_t old=epoch.load();
        while(!epoch.compare_exchange_weak(old,((old>>1)+1)*2+(old&1),std::memory_order_acq_rel)){}
    }
    static uint64_t stamp(uint64_t e,uint32_t id){return uint64_t(uint32_t(e>>1))<<32|id;}
    bool stopInfo(uint64_t e,uint32_t id,int &transition,int &curve)const{
        if(!id)return false;uint32_t generation=uint32_t(e>>1);
        unsigned start=(id*2654435761u)%Stops;
        for(unsigned n=0;n<Stops;++n){auto &cell=stopped[(start+n)%Stops];uint64_t v=cell.key.load(std::memory_order_acquire);
            if(uint32_t(v>>32)!=generation)return false;
            if(uint32_t(v)==id){
                uint64_t p=cell.parameters.load(std::memory_order_acquire);
                if(cell.key.load(std::memory_order_acquire)!=v)return false;
                transition=int32_t(p>>32);curve=int32_t(p);return true;
            }
        }return false;
    }
    bool wasStopped(uint64_t e,uint32_t id)const{int t,c;return stopInfo(e,id,t,c);}
    bool rememberStop(uint64_t e,uint32_t id,int transition,int curve){
        uint32_t generation=uint32_t(e>>1);unsigned start=(id*2654435761u)%Stops;
        uint64_t parameters=uint64_t(uint32_t(transition))<<32|uint32_t(curve);
        for(unsigned n=0;n<Stops;++n){
            auto &cell=stopped[(start+n)%Stops];uint64_t value=cell.key.load(std::memory_order_acquire);
            for(unsigned retry=0;retry<64;++retry){
                uint32_t old=uint32_t(value>>32);
                if(old>generation||epoch.load()!=e)return false;
                if(old==generation&&uint32_t(value)!=id)break;
                if(old&&uint32_t(value)==0)break; // Never steal an in-flight writer's slot.
                uint64_t claimed=uint64_t(generation)<<32;
                if(cell.key.compare_exchange_weak(value,claimed,std::memory_order_acq_rel)){
                    cell.parameters.store(parameters,std::memory_order_release);
                    cell.key.store(stamp(e,id),std::memory_order_release);return true;
                }
            }
        }overflow();return false;
    }
    static void unpin(Token &t){
        unsigned pinned=Pinned;
        if(!t.state.compare_exchange_strong(pinned,Playing,std::memory_order_acq_rel)&&pinned==PinnedEnd)
            t.state.store(Free,std::memory_order_release);
    }
    void applyCancellation(Token &t,uint64_t e){
        int time=0,curve=4;bool cancel=t.epoch!=e||!(e&1);
        if(cancel){if(t.cancelEpoch==e)return;t.cancelEpoch=e;}
        else {if(t.stopIssued||!stopInfo(t.epoch,t.original,time,curve))return;t.stopIssued=true;}
        if(t.playing&&api.stop)api.stop(api.context,t.playing,time,curve);
    }
    void stopMatching(uint64_t e,uint32_t original,int transition,int curve,bool maintenance){
        for(auto &t:tokens){
            unsigned playing=Playing;
            if(!t.state.compare_exchange_strong(playing,Pinned,std::memory_order_acq_rel))continue;
            if(maintenance)applyCancellation(t,e);
            else if(t.epoch==e&&t.original==original){
                t.stopIssued=true;if(t.playing&&api.stop)api.stop(api.context,t.playing,transition,curve);
            }
            unpin(t);
        }
    }
    bool hasOriginal(uint64_t e,uint32_t id){
        for(auto &t:tokens){unsigned playing=Playing;
            if(!t.state.compare_exchange_strong(playing,Pinned,std::memory_order_acq_rel)){
                // Another observer may briefly own the slot. Retain this stop
                // conservatively rather than reading unpinned metadata or
                // losing a real stop while maintenance releases its pin.
                if(playing!=Free&&playing!=EarlyEnd)return true;
                continue;
            }
            bool match=t.epoch==e&&t.original==id;unpin(t);if(match)return true;
        }return false;
    }
public:
    ImmediateDispatcher(const Mapping *table,size_t n,API a):mapping(table),count(n),api(a){}
    Pending begin(uint32_t event){return Pending(mapped(event)?this:nullptr,epoch.load(std::memory_order_acquire));}
    void setEnabled(bool enabled){uint64_t old=epoch.load(std::memory_order_acquire);
        while(bool(old&1)!=enabled&&!epoch.compare_exchange_weak(old,((old>>1)+1)*2+enabled,std::memory_order_acq_rel)){}
    }
    uint64_t generation()const{return epoch.load(std::memory_order_acquire);}
    uint64_t dropped()const{return drops.load();}
    size_t active()const{size_t n=0;for(auto &t:tokens)n+=t.state.load()!=Free;return n;}
    static void ended(Token *t){
        if(!t)return;unsigned state=t->state.load(std::memory_order_acquire);
        for(;;){
            unsigned next=state==Posting?EarlyEnd:state==Playing?Free:state==Pinned?PinnedEnd:state;
            if(next==state)return;
            if(t->state.compare_exchange_weak(state,next,std::memory_order_acq_rel))return;
        }
    }
    bool submit(uint32_t event,uint64_t owner,uint32_t original,uint32_t type=0,uint64_t value=0){
        uint64_t e=epoch.load(std::memory_order_acquire);
        if(!(e&1)||!event||uint32_t(e>>1)==0xffffffffu||(scopeEpoch!=UINT64_MAX&&scopeEpoch!=e))return false;
        size_t a=0,b=count;while(a<b){size_t m=(a+b)/2;if(mapping[m].event<event)a=m+1;else b=m;}
        if(a==count||mapping[a].event!=event||wasStopped(e,original))return false;
        Token *token=nullptr;
        for(auto &candidate:tokens){unsigned free=Free;
            if(candidate.state.compare_exchange_strong(free,Preparing,std::memory_order_acq_rel)){token=&candidate;break;}}
        if(!token){overflow();return false;}
        token->epoch=e;token->original=original;token->playing=0;token->stopIssued=false;token->cancelEpoch=0;
        Command c;c.epoch=e;c.event=event;c.privateEvent=mapping[a].privateEvent;c.owner=owner;
        c.originalPlaying=original;c.customType=type;c.customValue=value;
        if(epoch.load()!=e||wasStopped(e,original)){token->state=Free;return false;}
        token->state.store(Posting,std::memory_order_release);
        uint32_t id=api.post?api.post(api.context,c,token):0;token->playing=id;
        if(!id)token->state.store(Free,std::memory_order_release);
        else{
            unsigned posting=Posting;
            if(!token->state.compare_exchange_strong(posting,Playing,std::memory_order_acq_rel)&&posting==EarlyEnd)
                token->state.store(Free,std::memory_order_release);
            // Pin before reading after publication: an end callback may have
            // returned this slot to another poster while PostEvent returned.
            unsigned playing=Playing;
            if(token->state.compare_exchange_strong(playing,Pinned,std::memory_order_acq_rel)){
                if(token->playing==id&&token->epoch==e)applyCancellation(*token,epoch.load());
                unpin(*token);
            }
        }
        return id!=0;
    }
    bool stopOriginal(uint32_t id,int transition=0,int curve=4){
        uint64_t e=epoch.load();if(!id||!(e&1))return false;
        if(!pendingGames.load(std::memory_order_acquire)&&!hasOriginal(e,id))return false;
        if(!rememberStop(e,id,transition,curve))return false;
        stopMatching(e,id,transition,curve,false);return true;
    }
    void drain(){stopMatching(epoch.load(std::memory_order_acquire),0,0,4,true);}
};
}
