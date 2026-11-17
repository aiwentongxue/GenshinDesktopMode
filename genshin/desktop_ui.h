namespace DesktopUIGeometry {static void Before(int,int);static void After(int);}
#include "control_device_policy.h"
#include "desktop_controller_layout.h"
// UI layout and input type are separate in this build. Use the game's own
// complete hot-swap operation so existing views and future views agree.
namespace DesktopUI {
static bool (*originalLayoutSwitch)(void *,int,int,void *);
static void (*originalControlSwitch)(void *,int,bool,void *);
static void (*beforeInputSwitch)(int);
static void (*afterInputSwitch)(int);
static bool SwitchInputMode(int requestedMode);
static bool TryEnable(void);
static uint64_t deviceLayoutRepairRevision;
static void QueueDeviceLayoutRepair(int mode);
static thread_local unsigned layoutSwitchDepth;
static uint64_t completedLayoutSwitches;
struct LayoutSwitchScope {
    LayoutSwitchScope(){++layoutSwitchDepth;}
    ~LayoutSwitchScope(){--layoutSwitchDepth;}
};
static void PrepareInputSwitch(int inputMode) {
    char *geometryOwner=VerifiedInputOwner();
    if(geometryOwner)DesktopUIGeometry::Before(*(int *)(geometryOwner+GenshinOffsets().ownerInputMode),inputMode);
    bool wasController=NativeBridge::controllerInputActive.load();
    // The callback needs the previous value to release native mouse state.
    if(beforeInputSwitch) beforeInputSwitch(inputMode);
    NativeBridge::controllerInputActive=inputMode==3;
    if(wasController!=(inputMode==3)) RefreshPointerLock();
}
static void CompleteInputSwitch(void *owner) {
    // A requested mode is not a committed mode. In particular, a full hot-swap
    // can be refused while the settings page is closing. Reconcile ownership
    // from the native owner after the outermost switch, including refusals.
    if(layoutSwitchDepth || !owner) return;
    int actual=*(int *)((char *)owner+GenshinOffsets().ownerInputMode);
    NativeBridge::controllerInputActive=actual==3;
    if(afterInputSwitch) afterInputSwitch(actual);
    DesktopUIGeometry::After(actual);
    RefreshPointerLock();
}
static bool SwitchPCLayout(void *owner,int requestedLayout,int inputMode,void *method) {
    // Keep native input readers and bindings. Controller views use the game's
    // desktop controller family; keyboard/mouse retains the existing PC view.
    int previousMode=*(int *)((char *)owner+GenshinOffsets().ownerInputMode);
    inputMode=DesktopControlDevicePolicy::StartupInput(inputMode,
        DesktopControlPreference::bootstrapReady.load(),DesktopControlPreference::gameplayReady.load());
    inputMode=DesktopControlDevicePolicy::DesktopFallback(previousMode,inputMode);
    int layout=DesktopControllerLayout::Select(inputMode,requestedLayout);
    bool accepted;
    {
        LayoutSwitchScope scope;
        PrepareInputSwitch(inputMode);
        accepted=originalLayoutSwitch(owner,layout,inputMode,method);
    }
    if(accepted)++completedLayoutSwitches;
    CompleteInputSwitch(owner);
    return accepted;
}
static void SwitchControlMode(void *owner,int inputMode,bool partial,void *method) {
    // This entry is also called directly by the game's controller detection.
    // Release mouse overrides before the game applies its own controller state.
    int previousMode=*(int *)((char *)owner+GenshinOffsets().ownerInputMode);
    int discovered=inputMode;
    inputMode=DesktopControlDevicePolicy::StartupInput(inputMode,
        DesktopControlPreference::bootstrapReady.load(),DesktopControlPreference::gameplayReady.load());
    if(discovered==3&&inputMode==2){static unsigned reports=0;if(++reports<=3)
        Log(@"desktop_controller_startup deferred=1 native_discovery=1 login_mode=keyboard saved_preference_unchanged=1");}
    inputMode=DesktopControlDevicePolicy::DesktopFallback(previousMode,inputMode);
    bool fullRebuild=DesktopControlDevicePolicy::NeedsDesktopRebuild(previousMode,inputMode,layoutSwitchDepth!=0);
    PrepareInputSwitch(inputMode);
    originalControlSwitch(owner,inputMode,partial,method);
    CompleteInputSwitch(owner);
    // Mode-only selection skips the complete layout hot-swap. Rebuild views
    // once so the party slots, controller hints, canvas and platform adaptors
    // all select the same native desktop family.
    // A real full hot-swap in the same action supersedes this deferred repair.
    if(fullRebuild) QueueDeviceLayoutRepair(inputMode);
}
static bool InstallSwitchHook(uintptr_t logical,void *replacement,void **original) {
    if(*original) return true;
    void *target=EngineFunction<void *>(logical);
    if(!RuntimePrologueMatches(logical,target)) {
        Log(@"desktop_ui_device_hook refused_unexpected_code logical=0x%llx",logical);return false;
    }
    // All four profiled builds start with four stack/register instructions.
    // Refuse a different prologue instead of relocating relative instructions.
    const uint32_t *code=(const uint32_t *)target;
    bool safe=true;
    for(unsigned i=0;i<4;++i) {
        uint32_t instruction=code[i];
        bool stackSubtract=(instruction&0xffc003ffu)==0xd10003ffu;
        bool registerPair=(instruction&0xffc003e0u)==0xa90003e0u ||
                          (instruction&0xffc003e0u)==0xa98003e0u;
        safe&=stackSubtract || registerPair;
    }
    if(!safe) {Log(@"desktop_ui_device_hook refused_relative_prologue logical=0x%llx",logical);return false;}
    void *trampoline=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(trampoline==MAP_FAILED) return false;
    memcpy(trampoline,target,16);
    NativeBridge::AbsoluteJump((char *)trampoline+16,(uintptr_t)target+16);
    sys_icache_invalidate(trampoline,32);
    if(mprotect(trampoline,4096,PROT_READ|PROT_EXEC)) {munmap(trampoline,4096);return false;}
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)replacement);
    *original=trampoline;
    if(!NativeBridge::PatchCode(target,jump,16)) {
        *original=nullptr;munmap(trampoline,4096);return false;
    }
    return true;
}
static void InstallLayoutSwitch(void) {
    bool layout=InstallSwitchHook(0x1068c8584,(void *)SwitchPCLayout,(void **)&originalLayoutSwitch);
    bool control=InstallSwitchHook(0x1068c8ab0,(void *)SwitchControlMode,(void **)&originalControlSwitch);
    Log(@"desktop_ui_device_layout installed=%d control_hook=%d keyboard_layout=PC controller_layout=%s input_mode_preserved=1",layout,control,DesktopControllerLayout::Ready()?"desktop_controller":"PC_fallback");
}
static int DefaultPCLayout(void *,void *) {
    static bool logged=false;
    if(!logged) {logged=true;Log(@"desktop_ui_initial_layout=PC saved_mode=%d restore_after_main_scene=1",DesktopControlPreference::SavedMode());}
    return 1;
}
static void InstallInitialLayout(void) {
    if(!IsCompatibleRuntime()) return;
    DesktopControllerLayout::Install();
    // This is the UI manager's layout selector, not Application.platform.
    // Install before its initializer stores the default and constructs views.
    void *target=EngineFunction<void *>(0x10342b2c0);
    const uint8_t expected[]={0xf4,0x4f,0xbe,0xa9,0xfd,0x7b,0x01,0xa9,
        0xfd,0x43,0x00,0x91,0xf3,0x03,0x00,0xaa};
    if(!RuntimePrologueMatches(0x10342b2c0,target)) {
        Log(@"desktop_ui_initial refused_unexpected_code");return;
    }
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)DefaultPCLayout);
    Log(@"desktop_ui_initial installed=%d",NativeBridge::PatchCode(target,jump,16));
    InstallLayoutSwitch();
}
static bool SwitchInputMode(int requestedMode) {
    if(!IsCompatibleRuntime() || (requestedMode!=2 && requestedMode!=3)) return false;
    char *owner=VerifiedInputOwner(),*ui=VerifiedUIOwner();
    if(!owner || !ui) return false;
    auto lookup=EngineFunction<void *(*)(void *,const char *,int,int)>(0x10f0572e8);
    auto invoke=EngineFunction<void *(*)(void *,void *,void **,void **)>(0x10e9bb768);
    void *method=lookup(*(void **)owner,LayoutSwitchMethodName(),2,0);
    if(!method || ManagedMethodCode(method)!=(uintptr_t)EngineFunction<void *>(0x1068c8584)) return false;
    int layout=DesktopControllerLayout::Select(requestedMode,1);void *args[]={&layout,&requestedMode},*exception=nullptr;
    void *result=invoke(method,owner,args,&exception);
    if(exception) CompleteInputSwitch(owner);
    bool accepted=!exception && result && *(bool *)((char *)result+16) &&
        *(int *)(ui+GenshinOffsets().uiLayout)==layout &&
        *(int *)(owner+GenshinOffsets().ownerInputMode)==requestedMode;
    Log(@"desktop_ui_select_device requested=%d layout=%d actual=%d accepted=%d exception=%p",
        requestedMode,*(int *)(ui+GenshinOffsets().uiLayout),
        *(int *)(owner+GenshinOffsets().ownerInputMode),accepted,exception);
    return accepted;
}
static void RepairDeviceLayout(int mode,uint64_t revision,uint64_t completed,unsigned attempt) {
    if(revision!=deviceLayoutRepairRevision) return;
    if(mode==3&&!DesktopControlPreference::gameplayReady.load())return;
    char *input=VerifiedInputOwner(),*ui=VerifiedUIOwner();
    // Never resurrect a superseded selection or change modes on a new scene.
    if(!input||!ui||*(int *)(input+GenshinOffsets().ownerInputMode)!=mode) return;
    int expected=DesktopControllerLayout::Select(mode,1);
    bool accepted=completedLayoutSwitches!=completed &&
        *(int *)(ui+GenshinOffsets().uiLayout)==expected;
    if(!accepted) accepted=SwitchInputMode(mode);
    else CompleteInputSwitch(input);
    Log(@"desktop_ui_device_rebuild mode=%d attempt=%u accepted=%d",mode,attempt+1,accepted);
    if(!accepted && attempt<7)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,100*NSEC_PER_MSEC),dispatch_get_main_queue(), ^{
            RepairDeviceLayout(mode,revision,completed,attempt+1);
        });
}
static void QueueDeviceLayoutRepair(int mode) {
    if(mode!=2&&mode!=3) return;
    uint64_t revision=++deviceLayoutRepairRevision,completed=completedLayoutSwitches;
    dispatch_async(dispatch_get_main_queue(), ^{RepairDeviceLayout(mode,revision,completed,0);});
}
static bool TryEnable(void) {
    if(!IsCompatibleRuntime()) return false;
    auto findClass=EngineFunction<void *(*)(const char *,const char *,const char *)>(0x10f057bd4);
    auto lookup=EngineFunction<void *(*)(void *,const char *,int,int)>(0x10f0572e8);
    auto invoke=EngineFunction<void *(*)(void *,void *,void **,void **)>(0x10e9bb768);
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    char *input=VerifiedInputOwner();
    char *ui=VerifiedUIOwner();
    if(!input || !ui || strcmp(className(*(void **)input),InputOwnerClassName())) return false;

    static int pcLayout=-1;
    static bool inspected=false;
    if(!inspected) {
        inspected=true;
        auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
        auto fieldName=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
        auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
        auto value=EngineFunction<void (*)(void *,void *)>(0x10ea03c4c);
        void *cls=findClass("Assembly-CSharp.dll","",UILayoutClassName()),*it=nullptr;
        if(!cls&&IsGlobalModernRuntime())
            cls=ManagedFieldClassAt(*(void **)ui,GenshinOffsets().uiLayout,UILayoutClassName());
        if(cls) while(void *field=next(cls,&it)) {
            if(!(flags(field)&0x40)) continue;
            int member=0;value(field,&member);
            const char *name=fieldName(field);
            Log(@"desktop_ui_enum member=%s value=%d",name,member);
            if(!strcmp(name,"PC")) pcLayout=member;
        }
        Log(@"desktop_ui_owner type=%s layout=%d input=%d",className(*(void **)ui),
            *(int *)(ui+GenshinOffsets().uiLayout),*(int *)(input+GenshinOffsets().ownerInputMode));
    }
    // Refuse to guess an enum value on an unexpected metadata layout.
    if(pcLayout!=1) return false;
    int currentMode=*(int *)(input+GenshinOffsets().ownerInputMode);
    bool restoreController=!DesktopControlPreference::startupRestored.load()&&DesktopControlPreference::LaunchMode()==3;
    if(restoreController&&(!DesktopControlPreference::bootstrapReady.load()||!DesktopControlPreference::gameplayReady.load()))return false;
    int desktopMode=restoreController?3:(currentMode==3?3:2);
    desktopMode=DesktopControlDevicePolicy::StartupInput(desktopMode,
        DesktopControlPreference::bootstrapReady.load(),DesktopControlPreference::gameplayReady.load());
#if defined(GDM_CONTROLLER_UI_PREVIEW)
    // Local, compile-time UI preview only. No controller is registered and no
    // stored game preference is written. The distributable omits this branch.
    desktopMode=3;
#endif
    int desiredLayout=DesktopControllerLayout::Select(desktopMode,pcLayout);
    bool alreadySelected=*(int *)(ui+GenshinOffsets().uiLayout)==desiredLayout && currentMode==desktopMode;
    if(alreadySelected&&!restoreController) {
        // A controller can be restored by the game before this plugin's
        // deferred startup observer. Synchronize ownership even when the
        // native mode/layout already match and no hot-swap is necessary.
        PrepareInputSwitch(desktopMode);CompleteInputSwitch(input);
        return true;
    }

    // Wait for the actual gameplay HUD; do not rebuild login/loading screens.
    void *go=findClass("UnityEngine.CoreModule.dll","UnityEngine","GameObject");
    void *find=go?lookup(go,"Find",1,0):nullptr;
    if(!find) return false;
    void *name=EngineFunction<void *(*)(const char *)>(0x10e9bb794)("InLevelMainPage");
    void *findArgs[]={name},*exception=nullptr;
    void *root=invoke(find,nullptr,findArgs,&exception);
    if(!root || exception) return false;

    if(restoreController) {
        char *game=*(char **)(input+GenshinOffsets().ownerGameInput);
        void *player=game?*(void **)(game+GenshinOffsets().inputPlayer):nullptr;
        void *controllers=player?*(void **)((char *)player+GenshinOffsets().playerControllers):nullptr;
        if(!controllers)return false;
        void *countMethod=lookup(*(void **)controllers,"get_joystickCount",0,0);
        if(!countMethod||!ManagedMethodCode(countMethod))return false;
        void *count=invoke(countMethod,controllers,nullptr,&exception);
        if(exception||!count||*(int *)((char *)count+16)<=0)return false;
        if(alreadySelected) {
            PrepareInputSwitch(desktopMode);CompleteInputSwitch(input);DesktopControlPreference::startupRestored=true;
            Log(@"control_preference restored=1 saved=3 actual=3 stage=main_scene already_selected=1");return true;
        }
    }

    void *method=lookup(*(void **)input,LayoutSwitchMethodName(),2,0);
    if(!method || ManagedMethodCode(method)!=(uintptr_t)EngineFunction<void *>(0x1068c8584)) {
        Log(@"desktop_ui refused_unexpected_hotswap_method");return false;
    }
    void *args[]={&desiredLayout,&desktopMode};exception=nullptr;
    int before=*(int *)(ui+GenshinOffsets().uiLayout);
    void *result=invoke(method,input,args,&exception);
    bool accepted=result && *(bool *)((char *)result+16);
    Log(@"desktop_ui_hotswap before=%d after=%d input=%d accepted=%d exception=%s",
        before,*(int *)(ui+GenshinOffsets().uiLayout),*(int *)(input+GenshinOffsets().ownerInputMode),accepted,
        exception?className(*(void **)exception):"none");
    bool selected=!exception&&*(int *)(ui+GenshinOffsets().uiLayout)==desiredLayout&&
        *(int *)(input+GenshinOffsets().ownerInputMode)==desktopMode;
    if(selected&&restoreController) {
        DesktopControlPreference::startupRestored=true;
        Log(@"control_preference restored=1 saved=3 actual=3 stage=main_scene accepted=%d",accepted);
    }
    return selected;
}
static void Install(void) {
    static unsigned attempts=0;
    if(TryEnable()) return;
    // Retain pending controller restoration across a long login. The native
    // main-scene root and joystick readiness are required before switching.
    [NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer *timer) {
        bool pending=!DesktopControlPreference::startupRestored.load()&&DesktopControlPreference::LaunchMode()==3;
        if(TryEnable() || (++attempts>=1200&&!pending)) [timer invalidate];
    }];
}
}
