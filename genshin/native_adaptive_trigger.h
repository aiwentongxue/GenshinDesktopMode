#pragma once
#include "haptic_hook_code.h"
#include "native_trigger_profile.h"
#include "ps_trigger_backend.h"
namespace NativeTrigger {
static bool installed,refused;
static void (*originalUpdate)(void *,void *);
static bool (*originalCapability)(void *,void *);
static void *ownerClass,*updateInfo,*capabilityInfo,*inputInfo,*bindingsInfo;
static void *bindingsType,*createInstanceInfo;
static Pair lastRequested{};static uint64_t lastGeneration=~uint64_t(0);
static double nextInstallAt,nextReportAt;static unsigned reports;
static std::atomic<uint64_t> updates{0},snapshots{0},sourceEffects{0},rejected{0},bindingChecks{0},bindingMisses{0};
struct BindingCache {int action=0;unsigned side=0;bool valid=false,result=false;double until=0;};
static BindingCache bindings[2];
static size_t ownerStateOffset,leftListOffset,rightListOffset,ownerEnabledOffset;
static void *stateClass,*effectListClass;
struct Shape {void *cls=nullptr;Mode mode=Mode::Off;size_t action=0,has=0,start=0,p0=0,p1=0;bool valid=false;};
static Shape shapes[8];
static void *Field(void *cls,const char *wanted,const char *type,size_t &offset) {
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto name=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
    auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
    auto at=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);
    auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
    auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    for(unsigned depth=0;cls&&depth<16;cls=parent(cls),++depth){void *it=nullptr;
        while(void *f=next(cls,&it))if(!(flags(f)&0x10)&&(!wanted||!strcmp(name(f),wanted))) {
            void *c=from(ManagedFieldType(f));unsigned n=at(f);
            if(c&&!strcmp(SceneUIProbe::className(c),type)&&n>=16&&n<8192){offset=n;return c;}
        }
    }return nullptr;
}
static Shape *ShapeFor(void *cls) {
    for(auto &s:shapes)if(s.cls==cls)return s.valid?&s:nullptr;
    Shape *slot=nullptr;for(auto &s:shapes)if(!s.cls){slot=&s;break;}if(!slot)return nullptr;
    auto &s=*slot;s.cls=cls;const char *name=SceneUIProbe::className(cls);size_t nullable=0,value=0,has=0,priority=0;
    void *n=Field(cls,"KJKJJEIFMNH","Nullable`1",nullable);
    if(!n||!Field(n,nullptr,"OFGKAILMOFA",value)||!Field(n,nullptr,"Boolean",has)||
       value<16||has<16||value-16>4||has-16>4||
       !Field(cls,"JPPJNDKGAPM","Single",s.start)||!Field(cls,"BIBKKIAPNCE","Int32",priority))return nullptr;
    s.action=nullable+value-16;s.has=nullable+has-16;
    if(!strcmp(name,"FAPOMKLAPGO")&&Field(cls,"ENOANFBKALA","Single",s.p0))s.mode=Mode::Feedback;
    else if(!strcmp(name,"LALHJLIAIAH")&&Field(cls,"ENOANFBKALA","Single",s.p0)&&Field(cls,"KAEKMOPCJGC","Single",s.p1))s.mode=Mode::Vibration;
    else if(!strcmp(name,"FAMIBPFOABN")&&Field(cls,"FMKHJAPHJFD","Single",s.p0)&&Field(cls,"CEADPHMAHDP","Single",s.p1))s.mode=Mode::Weapon;
    else return nullptr;
    s.valid=true;Log(@"native_trigger effect_schema class=%s mode=%u original_evaluated_values=1",name,unsigned(s.mode));return &s;
}
static const NativeTriggerCode *Profile(){return IsGlobalModernRuntime()?NativeGlobalTriggerCode:NativeCNTriggerCode;}
static bool ProfileMatches() {
    if(!IsModernRuntime())return false;
    for(unsigned i=0;i<4;++i){auto &p=Profile()[i];const void *code=RuntimeImageRange(p.address,32,false);if(!code||memcmp(code,p.bytes,32))return false;}
    return true;
}
// Only atomic/backend state is touched here; focus and input callbacks need
// not run on the game thread. Managed caches are cleared by Snapshot below.
static void Suspend(){PSTriggerBackend::Suspend();}
static bool Setting() {
    void *data=NativeVibrationSettings::General();
    return data&&HasManagedField(*(void **)data,"enableTriggerEffect",344,"Boolean")&&*(bool *)((char *)data+344);
}
static bool Active() {
    return PSTriggerBackend::capable&&Setting()&&NativeBridge::controllerInputActive.load()&&
        NativeBridge::IsForeground()&&!NativeBridge::nativeOverlayActive.load();
}
static void *BindingList() {
    if(!bindingsType||!createInstanceInfo)return nullptr;
    void *args[]={bindingsType};void *list=SceneUIProbe::Call(createInstanceInfo,nullptr,args);
    return list&&HasManagedField(*(void **)list,nullptr,16,"Int32[]")&&
        HasManagedField(*(void **)list,nullptr,24,"Int32")?list:nullptr;
}
static bool ActionBound(void *owner,int action,unsigned side) {
    auto &cache=bindings[side];double now=CACurrentMediaTime();
    if(cache.valid&&cache.action==action&&cache.side==side&&now<cache.until)return cache.result;
    ++bindingChecks;bool result=false;
    // Managed scratch objects remain on this attached main-thread stack only;
    // no unrooted managed object is retained between callbacks.
    void *input=SceneUIProbe::Call(inputInfo,owner),*list=BindingList();
    if(input&&list) {
        int controllerType=2;void *args[]={&controllerType,&action,list};
        if(ControlDevice::VoidCall(bindingsInfo,input,args)) {
            char *items=*(char **)((char *)list+16);int count=*(int *)((char *)list+24);
            uint64_t length=items?*(uint64_t *)(items+24):0;
            if(count>=0&&count<=4096&&uint64_t(count)<=length&&length<=65536)
                for(int i=0;i<count;++i)if(((int *)(items+32))[i]==int(side?0x27:0x26)){result=true;break;}
        }
    }
    cache={action,side,true,result,now+.25};return result;
}
static Effect ReadEffect(void *value,void *owner,unsigned side) {
    if(!value)return {};
    Shape *s=ShapeFor(*(void **)value);if(!s){++rejected;return {};}
    char *p=(char *)value;
    // The game's Nullable<input action> has value first, then hasValue.
    if(*(bool *)(p+s->has)&&!ActionBound(owner,*(int *)(p+s->action),side)){++bindingMisses;return {};}
    Effect effect;
    if(!FromEvaluated(s->mode,*(float *)(p+s->start),*(float *)(p+s->p0),s->p1?*(float *)(p+s->p1):0,effect)){++rejected;return {};}
    ++sourceEffects;return effect;
}
static void *LastEffect(void *list) {
    if(!list||*(void **)list!=effectListClass)return nullptr;
    int count=*(int *)((char *)list+24);char *items=*(char **)((char *)list+16);
    uint64_t length=items?*(uint64_t *)(items+24):0;
    if(count<=0||count>4096||uint64_t(count)>length||length>65536)return nullptr;
    // Both native clients and the original PC update select LastOrDefault,
    // preserving the original priority/order and removal behavior.
    return ((void **)(items+32))[count-1];
}
static Pair ReadPair(void *owner) {
    Pair result{};if(!owner||*(void **)owner!=ownerClass||!ownerStateOffset)return result;
    // The native setter also mutes effects temporarily for action/UI states.
    // Respect it independently of the saved user preference.
    if(!ownerEnabledOffset||!*(bool *)((char *)owner+ownerEnabledOffset))return result;
    void *state=*(void **)((char *)owner+ownerStateOffset);
    if(!state||*(void **)state!=stateClass)return result;
    result[0]=ReadEffect(LastEffect(*(void **)((char *)state+leftListOffset)),owner,0);
    result[1]=ReadEffect(LastEffect(*(void **)((char *)state+rightListOffset)),owner,1);
    return result;
}
static void Snapshot(void *owner) {
    if(!installed||![NSThread isMainThread])return;
    bool active=Active();PSTriggerBackend::SetAllowed(active);
    if(!active){lastGeneration=~uint64_t(0);for(auto &b:bindings)b.valid=false;return;}
    uint64_t gen=PSTriggerBackend::generation.load();
    if(gen!=lastGeneration)for(auto &b:bindings)b.valid=false;
    ++snapshots;Pair pair=ReadPair(owner);
    if(pair!=lastRequested||gen!=lastGeneration) {
        lastRequested=pair;lastGeneration=gen;PSTriggerBackend::Submit(pair);
        // Dynamic authored parameters can change every frame. Do not turn
        // their update path into a per-frame file logger.
        double now=CACurrentMediaTime();
        if(reports<4||now>=nextReportAt){++reports;nextReportAt=now+5;
            Log(@"native_trigger original_action left_mode=%u right_mode=%u generation=%llu source_effects=%llu synthetic_buttons=0 physical_feedback_verified=0",unsigned(pair[0].mode),unsigned(pair[1].mode),gen,sourceEffects.load());}
    }
}
static void Update(void *owner,void *method) {
    originalUpdate(owner,method);++updates;
    // The authored ability mixin still evaluates dynamic floats, adds/removes
    // effects, sorts priorities and reaches this exact native update method.
    Snapshot(owner);
}
static bool Capability(void *owner,void *method) {
    return originalCapability(owner,method)||PSTriggerBackend::capable.load();
}
static void *Trampoline(void *code) {
    HapticHookCode relocated;if(!BuildHapticHookCode((const uint32_t *)code,(uintptr_t)code,relocated))return nullptr;
    void *p=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);if(p==MAP_FAILED)return nullptr;
    memcpy(p,relocated.bytes.data(),relocated.length);sys_icache_invalidate(p,relocated.length);
    if(mprotect(p,4096,PROT_READ|PROT_EXEC)){munmap(p,4096);return nullptr;}return p;
}
static bool TryInstall() {
    if(installed)return true;if(refused||NativeBridge::frames.load()<600)return false;
    double now=CACurrentMediaTime();if(now<nextInstallAt)return false;nextInstallAt=now+.5;
    char *owner=VerifiedInputOwner();if(!owner)return false;
    if(!ProfileMatches()){refused=true;Log(@"native_trigger refused=1 reason=code_profile");return false;}
    ownerClass=*(void **)owner;auto method=ControlDevice::MethodAt;
    stateClass=Field(ownerClass,"JBPBGCEKOFE","EKDEHFEPLNC",ownerStateOffset);
    if(!stateClass||!Field(ownerClass,"LIIOAFLGCOI","Boolean",ownerEnabledOffset))return false;
    effectListClass=Field(stateClass,"<ADCFAIJFJEH>k__BackingField","List`1",leftListOffset);
    if(!effectListClass||Field(stateClass,"<DDOCAJEFEIJ>k__BackingField","List`1",rightListOffset)!=effectListClass||
       !HasManagedField(effectListClass,nullptr,16,"PJFHODNAILN[]")||
       !HasManagedField(effectListClass,nullptr,24,"Int32"))return false;
    updateInfo=method(ownerClass,(uintptr_t)F<void *>(Profile()[0].address),"Void",{});
    capabilityInfo=method(ownerClass,(uintptr_t)F<void *>(Profile()[1].address),"Boolean",{});
    inputInfo=method(ownerClass,(uintptr_t)F<void *>(Profile()[2].address),"JGAPEPKBJJP",{});
    if(!updateInfo||!capabilityInfo||!inputInfo)return false;
    void *input=SceneUIProbe::Call(inputInfo,owner);if(!input)return false;
    bindingsInfo=method(*(void **)input,(uintptr_t)F<void *>(Profile()[3].address),"Void",{"ControllerType","OFGKAILMOFA","List`1"});
    void *activator=SceneUIProbe::findClass("mscorlib.dll","System","Activator");
    createInstanceInfo=activator?ControlDevice::Method(activator,"CreateInstance","Object",{"Type"}):nullptr;
    if(!bindingsInfo||!createInstanceInfo)return false;
    auto parameter=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    bindingsType=EngineFunction<void *(*)(void *)>(0x10e9bb7b8)(parameter(bindingsInfo,2));
    if(!BindingList())return false;
    auto *u=(void *)ManagedMethodCode(updateInfo),*c=(void *)ManagedMethodCode(capabilityInfo);
    originalUpdate=(decltype(originalUpdate))Trampoline(u);originalCapability=(decltype(originalCapability))Trampoline(c);
    if(!originalUpdate||!originalCapability){
        if(originalUpdate)munmap((void *)originalUpdate,4096);
        if(originalCapability)munmap((void *)originalCapability,4096);
        originalUpdate=nullptr;originalCapability=nullptr;refused=true;return false;
    }
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)Update);
    if(!NativeBridge::PatchCode(u,jump,16)){refused=true;return false;}
    NativeBridge::AbsoluteJump(jump,(uintptr_t)Capability);
    if(!NativeBridge::PatchCode(c,jump,16)) {
        NativeBridge::PatchCode(u,Profile()[0].bytes,16);refused=true;return false;
    }
    installed=true;
    Log(@"native_trigger chain_bound=1 source=original_controller_trigger_effect_mixin evaluated_parameters=1 native_priority_and_lifecycle=1 conditional_binding=1 setting=original_enableTriggerEffect original_setting_enabled=%d runtime_enable_offset=%lu original_runtime_enabled=%d profile=%s physical_feedback_verified=0",Setting(),ownerEnabledOffset,*(bool *)(owner+ownerEnabledOffset),RuntimeProfileName());
    return true;
}
static void Install() {
    PSTriggerBackend::Install();
    __block double nextStatus=0;
    [NSTimer scheduledTimerWithTimeInterval:1./60 repeats:YES block:^(NSTimer *){
        PSTriggerBackend::PollController();
        if(!TryInstall())return;
        void *owner=VerifiedInputOwner();Snapshot(owner);
        double now=CACurrentMediaTime();
        if(now>=nextStatus&&(PSTriggerBackend::capable||PSTriggerBackend::failures.load())) {
            nextStatus=now+10;
            bool runtimeEnabled=owner&&ownerEnabledOffset&&*(bool *)((char *)owner+ownerEnabledOffset);
            Log(@"native_trigger status capable=%d game_controller=%d allowed=%d original_setting_enabled=%d original_runtime_enabled=%d original_updates=%llu source_effects=%llu binding_misses=%llu rejected=%llu writes=%llu failures=%llu physical_feedback_verified=0",
                PSTriggerBackend::capable.load(),PSTriggerBackend::systemBackend.load(),PSTriggerBackend::allowed.load(),
                Setting(),runtimeEnabled,updates.load(),sourceEffects.load(),bindingMisses.load(),rejected.load(),
                PSTriggerBackend::writes.load(),PSTriggerBackend::failures.load());
        }
    }];
}
}
