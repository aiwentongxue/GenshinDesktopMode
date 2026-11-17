#pragma once
#include <new>
#include "native_motion_source_state.h"
#include "native_motion_source_profiles.h"

namespace NativeMotionSource {
// Private registration: never replace an existing game plugin. Only the
// remapped private bank references this ID. The ABI is checked independently
// against each installed client; it is not selected by version-number text.
constexpr unsigned pluginId=0xfd91,serializedPluginId=(pluginId<<16)|2;
static void *sourceTable[14],*parameterTable[7];
static std::atomic<uint64_t> voices{0},rendered{0},updates{0},invalid{0};
struct Params {void **table;void *allocator;State state;};
struct Source {void **table;void *allocator;Params *params;};
struct Format {uint32_t rate,channels;};
struct Buffer {float *samples;uint32_t channels,state;uint16_t maximum,valid;};
static_assert(offsetof(Buffer,maximum)==16&&offsetof(Buffer,valid)==18);

static void *Allocate(void *a,size_t n) {
    auto table=*(void ***)a;
    return ((void *(*)(void *,size_t))table[2])(a,n);
}
static void Free(void *a,void *p) {auto table=*(void ***)a;((void (*)(void *,void *))table[3])(a,p);}
static void ParameterDestructor(Params *){}
static void ParameterDelete(Params *p){if(p)Free(p->allocator,p);}
static int ParameterTerm(Params *p,void *){ParameterDelete(p);return 1;}
static void *CreateParams(void *allocator) {
    void *memory=Allocate(allocator,sizeof(Params));
    return memory?new(memory)Params{parameterTable,allocator,{}}:nullptr;
}
static void *Clone(Params *p,void *allocator) {
    auto next=(Params *)CreateParams(allocator);if(next)next->state=p->state;return next;
}
static int SetBlock(Params *p,const void *bytes,uint32_t size) {
    bool ok=p->state.block(bytes,size);if(!ok)++invalid;return ok?1:31;
}
static int ParameterInit(Params *p,void *,const void *bytes,uint32_t size) {
    return size?SetBlock(p,bytes,size):1;
}
static int SetParameter(Params *p,int16_t id,const void *bytes,uint32_t size) {
    bool ok=p->state.parameter(id,bytes,size);if(ok)++updates;else ++invalid;return ok?1:31;
}
static void SourceDestructor(Source *){}
static void SourceDelete(Source *s){if(s)Free(s->allocator,s);}
static int SourceTerm(Source *s,void *){SourceDelete(s);return 1;}
static int Reset(Source *){return 1;}
static int Initialize(Source *s,void *,void *,Params *p,Format *f) {
    if(!p||!f||!p->state.count||p->state.count>8){++invalid;return 31;}
    s->params=p;f->channels=p->state.count;++voices;return 1;
}
static float Duration(const Source *){return 0;}
static int StopLooping(Source *){return 1;}
// PC Motion Source returns AK_NotImplemented for virtual voice skipping.
// Returning AK_DataReady here would silently change its virtual-voice policy.
static int TimeSkip(Source *,uint32_t *){return 0;}
static void Execute(Source *s,Buffer *b) {
    if(!b)return;
    if(!s->params||!s->params->state.render(b->samples,b->channels&255,b->maximum)){
        b->valid=0;b->state=17;++invalid;return;
    }
    b->valid=b->maximum;b->state=45;rendered.fetch_add(b->valid,std::memory_order_relaxed);
}
static void *CreateSource(void *allocator) {
    void *memory=Allocate(allocator,sizeof(Source));
    return memory?new(memory)Source{sourceTable,allocator,nullptr}:nullptr;
}
static bool Register(void) {
    static bool attempted=false,registered=false;
    if(attempted)return registered;attempted=true;
    const auto *profile=IsGlobalModernRuntime()?MotionSourceGlobalProfile:MotionSourceCNProfile;
    for(unsigned i=0;i<6;++i){
        const void *p=RuntimeImageRange(profile[i].address,32,false);
        if(!p||memcmp(p,profile[i].bytes,32)){
            Log(@"native_source registered=0 reason=abi_profile address=0x%llx",(uint64_t)profile[i].address);return false;
        }
    }
    void *parameters[]={
        (void *)ParameterDestructor,(void *)ParameterDelete,(void *)SetParameter,(void *)Clone,
        (void *)ParameterInit,(void *)ParameterTerm,(void *)SetBlock};
    memcpy(parameterTable,parameters,sizeof(parameters));
    // The four inherited constant-return methods and plugin-info method are
    // reused from the checked native source ABI. None dereferences this.
    void *source[]={
        (void *)SourceDestructor,(void *)SourceDelete,(void *)SourceTerm,(void *)Reset,F<void *>(0x1108cdca0),
        F<void *>(0x11084f750),F<void *>(0x11084f758),(void *)Initialize,(void *)Duration,
        F<void *>(0x11084f768),(void *)StopLooping,F<void *>(0x11084f770),(void *)TimeSkip,(void *)Execute};
    memcpy(sourceTable,source,sizeof(source));
    int status=F<int (*)(unsigned,unsigned,unsigned,void *,void *)>(0x110903ce0)
        (2,0,pluginId,(void *)CreateSource,(void *)CreateParams);
    registered=status==1;
    Log(@"native_source registered=%d status=%d private_plugin=0x%x pc_driver_semantics=1 rtpc_and_time=original_engine physical_feedback_unverified=1",
        registered,status,serializedPluginId);
    return registered;
}
}
