#pragma once
#include "native_settings_profile.h"
namespace NativeVibrationSettings {
static bool installed=false,refused=false,initialized=false;
static std::atomic<bool> enabled{false};
static void (*onEnabledChanged)(bool);
static int lastLevel=-1,lastIntensity=-1;
static const NativeSettingCode *Profile(){return IsGlobalModernRuntime()?NativeGlobalSettingCode:NativeCNSettingCode;}
static bool CodeMatches(){
    if(!IsModernRuntime())return false;
    for(unsigned i=0;i<5;++i){auto &p=Profile()[i];const void *code=RuntimeImageRange(p.address,32,false);if(!code||memcmp(code,p.bytes,32))return false;}
    return true;
}
static char *Globals(){auto p=EngineFunction<char **>(0x115644a70);return p?*p:nullptr;}
static void *General(){
    char *g=Globals();if(!g)return nullptr;
    void *owner=*(void **)(g+(IsGlobalModernRuntime()?0x8e40:0x8dd0));if(!owner)return nullptr;
    void *data=F<void *(*)(void *,void *)>(Profile()[2].address)(owner,nullptr);
    if(!data||strcmp(SceneUIProbe::className(*(void **)data),"GeneralLocalDataItem")||
       !HasManagedField(*(void **)data,"vibrationLevel",168,"Int32")||
       !HasManagedField(*(void **)data,"vibrationIntensity",172,"Int32")||
       !HasManagedField(*(void **)data,"usingNewVibrationSetting",176,"Boolean"))return nullptr;
    return data;
}
static bool ApplyStored(){
    char *g=Globals();void *data=General();if(!g||!data)return false;
    char *owner=*(char **)(g+(IsGlobalModernRuntime()?0x8be0:0x8bd0));
    void *audio=owner?*(void **)(owner+(IsGlobalModernRuntime()?0x40:0xd8)):nullptr;if(!audio)return false;
    void *level=ControlDevice::MethodAt(*(void **)audio,(uintptr_t)F<void *>(Profile()[3].address),"Void",{});
    void *intensity=ControlDevice::MethodAt(*(void **)audio,(uintptr_t)F<void *>(Profile()[4].address),"Void",{});
    if(!level||!intensity)return false;
    // Original routines migrate legacy vibration preferences when needed and
    // update switches/RTPCs with the game's configured conversion. Never create
    // a second sensitivity multiplier or preference.
    return ControlDevice::VoidCall(level,audio)&&ControlDevice::VoidCall(intensity,audio);
}
static void Refresh(){
    void *data=General();if(!data){enabled=false;return;}
    int level=*(int *)((char *)data+168),intensity=*(int *)((char *)data+172);
    bool known=*(bool *)((char *)data+176)&&level>=0&&level<=2&&intensity>=0&&intensity<=5;
    bool next=known&&level!=2;
    if(enabled.exchange(next,std::memory_order_acq_rel)!=next&&onEnabledChanged)
        onEnabledChanged(next);
    if(level!=lastLevel||intensity!=lastIntensity){
        lastLevel=level;lastIntensity=intensity;
        Log(@"native_motion settings level=%d intensity=%d master_enabled=%d native_curve=1 original_bus_gates=1",level,intensity,enabled.load());
    }
}
static bool Enabled(){return enabled.load(std::memory_order_acquire);}
static void Install(){
    if(installed||refused)return;
    if(!CodeMatches()){refused=true;Log(@"native_motion settings_disabled=1 reason=code_profile");return;}
    if(!General())return;
    uint32_t replacement=0x52800000;unsigned patched=0;
    for(;patched<2;++patched)if(!NativeBridge::PatchCode(F<void *>(Profile()[patched].address),&replacement,4))break;
    if(patched!=2){
        for(unsigned i=0;i<patched;++i)NativeBridge::PatchCode(F<void *>(Profile()[i].address),Profile()[i].bytes,4);
        refused=true;Log(@"native_motion settings_disabled=1 reason=row_patch");return;
    }
    installed=true;initialized=ApplyStored();Refresh();
    [NSTimer scheduledTimerWithTimeInterval:.1 repeats:YES block:^(NSTimer *){
        if(!initialized)initialized=ApplyStored();Refresh();
    }];
    Log(@"native_motion settings_ui=original_level_and_intensity controller_mode_only=1 apply_stored=%d physical_feedback_unverified=1",initialized);
}
}
