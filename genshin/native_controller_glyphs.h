#pragma once
#include "native_controller_sprites.h"

// Correct the game's hardware glyph family, not its input mode or action map.
// Its existing setter performs the normal icon-change notifications.
namespace NativeControllerGlyphs {
using namespace SceneUIProbe;
static void (*originalSet)(void *,int,void *);
static void *gameClass,*setterInfo;
static size_t currentOffset,requestedOffset;
static bool installed;
static std::mutex stateMutex;
static void *lastGame;
static int lastNativeStyle;
static std::atomic<uint64_t> appliedRevision{0};
static std::atomic<bool> queued{false};
static std::atomic<unsigned> corrections{0};

static void QueueRefresh();
static int Correct(int value) {
    return DesktopControllerUI::NativeFamily(value,DesktopControllerIdentity::skin.load());
}
static void Set(void *game,int value,void *method) {
    if(!game||*(void **)game!=gameClass){originalSet(game,value,method);return;}
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastGame=game;lastNativeStyle=value;
    }
    int resolved=Correct(value);
    originalSet(game,resolved,method);
    QueueRefresh();
    if(value!=resolved&&corrections.fetch_add(1)<5)
        Log(@"controller_glyph native=%d resolved=%d physical_family=1 input_bindings_unchanged=1",value,resolved);
}
static void Refresh() {
    queued=false;
    if(!installed||!DesktopControlPreference::bootstrapReady.load()||
       !DesktopControlPreference::gameplayReady.load()||!NativeBridge::IsForeground())return;
    uint64_t rev=DesktopControllerIdentity::revision.load();
    if(appliedRevision.load()==rev)return;
    char *owner=VerifiedInputOwner();
    void *game=owner?*(void **)(owner+GenshinOffsets().ownerGameInput):nullptr;
    if(!game||*(void **)game!=gameClass)return;
    int value=*(int *)((char *)game+requestedOffset);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if(lastGame==game)value=lastNativeStyle;
    }
    if(value<1||value>4)return;
    int resolved=Correct(value);
    bool ok=true;
    if(*(int *)((char *)game+currentOffset)!=resolved||*(int *)((char *)game+requestedOffset)!=resolved){
        void *args[]={&value};
        ok=ControlDevice::VoidCall(setterInfo,game,args);
    }
    // USB and Bluetooth deliberately share native PS action bindings. Refresh
    // their artwork even when the native family setter quite correctly no-ops.
    bool sprites=ok&&NativeControllerSprites::RefreshImages(game);
    if(sprites&&*(int *)((char *)game+requestedOffset)==resolved)appliedRevision=rev;
    Log(@"controller_glyph refresh=%d native=%d resolved=%d skin=%u sprites=%d profile=%s",
        ok,value,resolved,(unsigned)DesktopControllerIdentity::skin.load(),sprites,RuntimeProfileName());
}
static void QueueRefresh() {
    if(appliedRevision.load()==DesktopControllerIdentity::revision.load()||queued.exchange(true))return;
    dispatch_async(dispatch_get_main_queue(),^{Refresh();});
}
static bool EnumMatches(void *method) {
    auto parameter=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto name=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
    auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
    auto value=EngineFunction<void (*)(void *,void *)>(0x10ea03c4c);
    void *cls=from(parameter(method,0));if(!cls||strcmp(className(cls),"AJEKFLBMPEI"))return false;
    unsigned found=0;void *it=nullptr;
    const char *names[]={"Invalid","Xbox360Joypad","DualShock4Joypad","OtherController","UnknownController"};
    while(void *f=next(cls,&it))if((flags(f)&0x50)==0x50){
        for(unsigned i=0;i<5;++i)if(!strcmp(name(f),names[i])){
            int v=-1;value(f,&v);if(v!=(int)i)return false;found|=1u<<i;
        }
    }
    return found==31;
}
static bool TryInstall() {
    if(installed)return true;
    if(!IsModernRuntime()||!NativeBridge::frames.load()||!DesktopControlPreference::bootstrapReady.load())return false;
    char *owner=VerifiedInputOwner();void *game=owner?*(void **)(owner+GenshinOffsets().ownerGameInput):nullptr;
    if(!game||strcmp(className(*(void **)game),GameInputClassName()))return false;
    void *cls=*(void **)game;
    const auto &entry=IsGlobalModernRuntime()?ControllerGlyphProfiles::setter_global:ControllerGlyphProfiles::setter_cn;
    const void *body=RuntimeImageRange(entry.address,sizeof(entry.expected),false);
    if(!body||memcmp(body,entry.expected,sizeof(entry.expected)))return false;
    auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
    void *method=nullptr;
    for(unsigned i=0;cls&&i<8;cls=parent(cls),++i){
        method=ControlDevice::MethodAt(cls,(uintptr_t)body,"Void",{"AJEKFLBMPEI"});if(method)break;
    }
    if(!method||!EnumMatches(method)||
       !NativeChatFocus::FieldOffset(*(void **)game,"NKBGFOEMIJP","AJEKFLBMPEI",currentOffset)||
       !NativeChatFocus::FieldOffset(*(void **)game,"LMPIPJPEFDN","AJEKFLBMPEI",requestedOffset)||
       currentOffset!=24||requestedOffset!=28)return false;
    if(!NativeControllerSprites::TryInstall(game))return false;
    void *target=(void *)ManagedMethodCode(method);
    if(!NativeCameraPointer::RegisterOnlyPrologue(target))return false;
    originalSet=(decltype(originalSet))NativeCameraPointer::Trampoline(target);if(!originalSet)return false;
    gameClass=*(void **)game;setterInfo=method;
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)Set);
    if(!NativeBridge::PatchCode(target,jump,sizeof(jump)))return false;
    installed=true;QueueRefresh();
    Log(@"controller_glyph installed=1 native_family_setter=1 physical_identity=1 ps_usb_bt=1 action_maps_unchanged=1 profile=%s",RuntimeProfileName());
    return true;
}
static void Install() {
    DesktopControllerIdentity::changed=QueueRefresh;
    DesktopControllerIdentity::InstallTransportMonitor();
    static bool refreshTimer;
    if(!refreshTimer){refreshTimer=true;
        [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *){QueueRefresh();}];
    }
    if(TryInstall())return;
    static bool waiting;if(waiting)return;waiting=true;
    __block unsigned attempts=0;
    [NSTimer scheduledTimerWithTimeInterval:.5 repeats:YES block:^(NSTimer *timer){
        if(TryInstall()||++attempts>=120){waiting=false;[timer invalidate];
            if(!installed)Log(@"controller_glyph installed=0 native_validation_not_ready=1");}
    }];
}
}
