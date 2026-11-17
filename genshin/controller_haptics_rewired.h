#pragma once
#include "haptic_hook_code.h"

namespace ControllerHaptics {
using namespace SceneUIProbe;
struct RumbleHook {void *method=nullptr,*original=nullptr;bool refused=false;int alias=-1;};
static RumbleHook rumbleHooks[9];
static bool rewiredAttempted;
static thread_local unsigned rumbleDepth,capabilityProbeDepth;
struct RumbleScope {
    bool outer=rumbleDepth++==0;
    ~RumbleScope(){--rumbleDepth;}
};
struct CapabilityProbeScope {
    CapabilityProbeScope(){++capabilityProbeDepth;}
    ~CapabilityProbeScope(){--capabilityProbeDepth;}
};
static bool JoystickObject(void *object) {
    return object&&!strcmp(className(*(void **)object),"Joystick")&&
        !strcmp(classSpace(*(void **)object),"Rewired");
}
static bool BridgeJoystick(void *object) {
    if(!Allowed()||!JoystickObject(object))return false;
    // A mobile driver can advertise motors while producing no physical output
    // on Catalyst. Capability alone is not proof of playback. Keep its native
    // state updates, and route actual commands through the selected Apple
    // controller backend whenever that backend is available.
    CapabilityProbeScope probe;
    bool native=false;
    if(rumbleHooks[7].original)
        native=((bool (*)(void *,void *))rumbleHooks[7].original)(object,rumbleHooks[7].method);
    else if(rumbleHooks[8].original)
        native=((int (*)(void *,void *))rumbleHooks[8].original)(object,rumbleHooks[8].method)>0;
    static bool logged=false;
    if(!logged) {logged=true;Log(@"controller_haptics rewired_output=apple native_advertised=%d capability_not_playback=1",native);}
    return true;
}
static void SetMotor(void *object,int motor,float level,void *method) {
    RumbleScope scope;bool bridge=scope.outer&&BridgeJoystick(object);
    if(scope.outer)ObserveNativeCommand();
    ((void (*)(void *,int,float,void *))rumbleHooks[0].original)(object,motor,level,method);
    if(bridge)SubmitMotor(object,motor,level,0,false);
}
static void SetMotorExclusive(void *object,int motor,float level,bool stopOthers,void *method) {
    RumbleScope scope;bool bridge=scope.outer&&BridgeJoystick(object);
    if(scope.outer)ObserveNativeCommand();
    ((void (*)(void *,int,float,bool,void *))rumbleHooks[1].original)(object,motor,level,stopOthers,method);
    if(bridge)SubmitMotor(object,motor,level,0,stopOthers);
}
static void SetMotorTimed(void *object,int motor,float level,float duration,void *method) {
    RumbleScope scope;bool bridge=scope.outer&&BridgeJoystick(object);
    if(scope.outer)ObserveNativeCommand();
    ((void (*)(void *,int,float,float,void *))rumbleHooks[2].original)(object,motor,level,duration,method);
    if(bridge)SubmitMotor(object,motor,level,duration,false);
}
static void SetMotorTimedExclusive(void *object,int motor,float level,float duration,bool stopOthers,void *method) {
    RumbleScope scope;bool bridge=scope.outer&&BridgeJoystick(object);
    if(scope.outer)ObserveNativeCommand();
    ((void (*)(void *,int,float,float,bool,void *))rumbleHooks[3].original)(object,motor,level,duration,stopOthers,method);
    if(bridge)SubmitMotor(object,motor,level,duration,stopOthers);
}
static void SetBoth(void *object,float left,float right,void *method) {
    RumbleScope scope;bool bridge=scope.outer&&BridgeJoystick(object);
    if(scope.outer)ObserveNativeCommand();
    ((void (*)(void *,float,float,void *))rumbleHooks[4].original)(object,left,right,method);
    if(bridge)SubmitPair(object,left,right,0,0);
}
static void SetBothTimed(void *object,float left,float right,float leftDuration,float rightDuration,void *method) {
    RumbleScope scope;bool bridge=scope.outer&&BridgeJoystick(object);
    if(scope.outer)ObserveNativeCommand();
    ((void (*)(void *,float,float,float,float,void *))rumbleHooks[5].original)(object,left,right,leftDuration,rightDuration,method);
    if(bridge)SubmitPair(object,left,right,leftDuration,rightDuration);
}
static void StopRumble(void *object,void *method) {
    RumbleScope scope;
    ((void (*)(void *,void *))rumbleHooks[6].original)(object,method);
    if(scope.outer&&JoystickObject(object))StopMotors(object);
}
static bool SupportsRumble(void *object,void *method) {
    bool native=((bool (*)(void *,void *))rumbleHooks[7].original)(object,method);
    // Capability belongs to the connected hardware. It must not turn false
    // while switching modes or inactive, when the game may cache this query.
    return native||(!capabilityProbeDepth&&rewiredBridgeReady&&capable.load()&&JoystickObject(object));
}
static int MotorCount(void *object,void *method) {
    int native=((int (*)(void *,void *))rumbleHooks[8].original)(object,method);
    return !capabilityProbeDepth&&rewiredBridgeReady&&capable.load()&&JoystickObject(object)?std::max(native,2):native;
}
static void *TypedRumbleMethod(void *cls,const char *name,const char *result,
                             std::initializer_list<const char *> parameters) {
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
    auto methodName=EngineFunction<const char *(*)(void *)>(0x10e9bb6f0);
    auto count=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    auto returns=EngineFunction<void *(*)(void *)>(0x10e9bb6e8);
    auto parameter=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    for(unsigned depth=0;cls&&depth<8;cls=parent(cls),++depth) {
        void *it=nullptr;
        while(void *method=next(cls,&it)) {
            if(strcmp(methodName(method),name)||count(method)!=parameters.size()||
               strcmp(className(from(returns(method))),result))continue;
            bool valid=true;unsigned index=0;
            for(const char *expected:parameters)
                if(strcmp(className(from(parameter(method,index++))),expected))valid=false;
            if(valid&&ManagedMethodCode(method))return method;
        }
    }
    return nullptr;
}
static bool PatchRumble(unsigned index,void *method,void *replacement) {
    if(!method)return false;
    RumbleHook &hook=rumbleHooks[index];
    if(hook.original)return true;
    if(hook.refused)return false;
    hook.method=method;
    void *code=(void *)ManagedMethodCode(method);
    uintptr_t slide=_dyld_get_image_vmaddr_slide(0),address=(uintptr_t)code;
    HapticHookCode relocated;
    if(address<slide || !RuntimeImageRange(address-slide,16,false) ||
       !BuildHapticHookCode((const uint32_t *)code,address,relocated)) {
        hook.refused=true;Log(@"controller_haptics rewired_hook=%u refused_code=1",index);return false;
    }
    void *original=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(original==MAP_FAILED)return false;
    memcpy(original,relocated.bytes.data(),relocated.length);sys_icache_invalidate(original,relocated.length);
    if(mprotect(original,4096,PROT_READ|PROT_EXEC)) {munmap(original,4096);return false;}
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)replacement);
    hook.original=original;
    if(!NativeBridge::PatchCode(code,jump,16)) {hook.original=nullptr;munmap(original,4096);return false;}
    Log(@"controller_haptics rewired_hook=%u installed=1 typed_metadata=1 relocated_pages=%u",index,relocated.relocatedPages);
    return true;
}
static bool TryInstallRewired(void) {
    if(rewiredBridgeReady)return true;
    if(!NativeBridge::frames.load())return false;
    void *cls=nullptr;
    for(const char *assembly:{"Rewired_Core.dll","Assembly-CSharp-firstpass.dll"})
        if((cls=findClass(assembly,"Rewired","Joystick")))break;
    if(!cls)return false;
    void *methods[]={
        TypedRumbleMethod(cls,"SetVibration","Void",{"Int32","Single"}),
        TypedRumbleMethod(cls,"SetVibration","Void",{"Int32","Single","Boolean"}),
        TypedRumbleMethod(cls,"SetVibration","Void",{"Int32","Single","Single"}),
        TypedRumbleMethod(cls,"SetVibration","Void",{"Int32","Single","Single","Boolean"}),
        TypedRumbleMethod(cls,"SetVibration","Void",{"Single","Single"}),
        TypedRumbleMethod(cls,"SetVibration","Void",{"Single","Single","Single","Single"}),
        TypedRumbleMethod(cls,"StopVibration","Void",{}),
        TypedRumbleMethod(cls,"get_supportsVibration","Boolean",{}),
        TypedRumbleMethod(cls,"get_vibrationMotorCount","Int32",{})};
    void *wrappers[]={(void *)SetMotor,(void *)SetMotorExclusive,(void *)SetMotorTimed,(void *)SetMotorTimedExclusive,
                      (void *)SetBoth,(void *)SetBothTimed,(void *)StopRumble,(void *)SupportsRumble,(void *)MotorCount};
    // Never enable an indefinite rumble path unless its matching stop entry is
    // installed. Never advertise support after only a partial setter install.
    if(!methods[6])return false;
    // Some IL2CPP overloads are only FMOV s1,wzr; B timed-overload (8 bytes).
    // Preserve the complete veneer and hook its typed destination instead.
    // A 16-byte overwrite here would corrupt that destination's prologue.
    if(methods[1]&&methods[3]&&rumbleHooks[1].alias<0) {
        uintptr_t source=ManagedMethodCode(methods[1]),timed=ManagedMethodCode(methods[3]);
        uintptr_t slide=_dyld_get_image_vmaddr_slide(0);
        if(source>=slide&&RuntimeImageRange(source-slide,8,false)&&
           HapticUntimedTail((const uint32_t *)source,source,timed)) {
            rumbleHooks[1].alias=3;
            Log(@"controller_haptics rewired_hook=1 covered_by=3 native_8byte_tail_preserved=1");
        }
    }
    if(!PatchRumble(6,methods[6],wrappers[6]))return false;
    unsigned found=0,patched=0;
    for(unsigned index=0;index<6;++index)if(methods[index]) {
        ++found;if(rumbleHooks[index].alias<0&&PatchRumble(index,methods[index],wrappers[index]))++patched;
    }
    unsigned covered=patched;
    for(unsigned index=0;index<6;++index)
        if(methods[index]&&rumbleHooks[index].alias>=0&&rumbleHooks[rumbleHooks[index].alias].original)++covered;
    bool settersReady=found>0&&found==covered;
    if(settersReady)for(unsigned index:{7u,8u})PatchRumble(index,methods[index],wrappers[index]);
    rewiredBridgeReady=settersReady&&rumbleHooks[7].original&&rumbleHooks[8].original;
    if(!rewiredAttempted||rewiredBridgeReady) {
        Log(@"controller_haptics rewired_ready=%d setters_found=%u installed=%u covered=%u support_getter=%d count_getter=%d profile=%s physical_output_unverified=1",
            rewiredBridgeReady.load(),found,patched,covered,rumbleHooks[7].original!=nullptr,rumbleHooks[8].original!=nullptr,RuntimeProfileName());
        rewiredAttempted=true;
    }
    return rewiredBridgeReady;
}
}
