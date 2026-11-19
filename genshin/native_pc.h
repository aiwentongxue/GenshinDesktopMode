// Genshin-only desktop mouse investigation and runtime control. The retained
// revision 16 stays available as the fallback while these routes are verified.
namespace NativePC {
using namespace SceneUIProbe;
static bool inspected;
static void *previousModule;
static bool wantNative=true,hooksInstalled;
static void *configuredModule,*configuredHybrid;
static bool oldAllowTouch,oldForce,oldVirtual;
static uint64_t mouseFrames,touchPasses,touchConsumed;
static void (*originalMouse)(void *,int,void *);
static bool (*originalTouch)(void *,void *);
struct Vector2 {float x,y;};
static char *InputOwner(void) {
    if(!IsCompatibleRuntime()) return nullptr;
    char *owner=VerifiedInputOwner();
    return owner && !strcmp(className(*(void **)owner),InputOwnerClassName())?owner:nullptr;
}
static void Describe(void *cls) {
    if(!cls) return;
    Log(@"native_pc_class namespace=%s name=%s",classSpace(cls),className(cls));
    Signatures(cls);
}
static void *MousePointer(void *module) {
    void *state=*(void **)((char *)module+80);if(!state) return nullptr;
    void *method=lookup(*(void **)state,"GetButtonState",1,0);if(!method) return nullptr;
    int left=0;void *args[]={&left},*button=Call(method,state,args);
    if(!button || strcmp(className(*(void **)button),"ButtonState")) return nullptr;
    void *data=*(void **)((char *)button+24);
    if(!data || strcmp(className(*(void **)data),"MouseButtonEventData")) return nullptr;
    void *pointer=*(void **)((char *)data+24);
    return pointer && !strcmp(className(*(void **)pointer),"PointerEventData")?pointer:nullptr;
}
static Vector2 ReadVector(void *object,const char *property) {
    void *box=Property(object,property);return box?*(Vector2 *)((char *)box+16):Vector2{};
}
static void Mouse(void *module,int id,void *method) {
    originalMouse(module,id,method);
    if(!NativeBridge::nativeDesktopMouseUI.load()) return;
    ++mouseFrames;
    if(!diagnosticsEnabled) return;
    void *pointer=MousePointer(module);
    if(!pointer) {
        if(mouseFrames<=3) Log(@"native_pc_mouse frame=%llu pointer_unavailable=1",mouseFrames);
        return;
    }
    Vector2 position=ReadVector(pointer,"get_position"),delta=ReadVector(pointer,"get_delta");
    bool dragging=Bool(pointer,"get_dragging");
    static bool previousDrag=false;
    if(mouseFrames<=4 || dragging!=previousDrag || mouseFrames%120==0)
        Log(@"native_pc_mouse frame=%llu position=(%.1f,%.1f) delta=(%.1f,%.1f) dragging=%d hover=%@ dragTarget=%@",
            mouseFrames,position.x,position.y,delta.x,delta.y,dragging,
            Name(Property(pointer,"get_pointerEnter")),Name(Property(pointer,"get_pointerDrag")));
    previousDrag=dragging;
}
static bool Touch(void *module,void *method) {
    bool consumed=originalTouch(module,method);
    if(NativeBridge::nativeDesktopMouseUI.load()) {
        ++touchPasses;touchConsumed+=consumed;
        if(touchPasses<=3) Log(@"native_pc_touch consumed=%d allow=%d",consumed,Bool(module,"get_allowTouchInput"));
    }
    return consumed;
}
static bool InstallObservers(void) {
    if(hooksInstalled) return true;
    struct Hook {uintptr_t va;const uint8_t *expected;void *replacement;void **original;};
    static const uint8_t mouseCode[]={0xff,0x03,0x01,0xd1,0xf6,0x57,0x01,0xa9,0xf4,0x4f,0x02,0xa9,0xfd,0x7b,0x03,0xa9};
    static const uint8_t touchCode[]={0xff,0xc3,0x03,0xd1,0xfa,0x67,0x0a,0xa9,0xf8,0x5f,0x0b,0xa9,0xf6,0x57,0x0c,0xa9};
    Hook hooks[]={
        {0x103a39088,mouseCode,(void *)Mouse,(void **)&originalMouse},
        {0x103a3894c,touchCode,(void *)Touch,(void **)&originalTouch}
    };
    for(auto hook:hooks) if(memcmp(EngineFunction<void *>(hook.va),hook.expected,16)) {
        Log(@"native_pc refused_unexpected_input_code va=0x%llx",hook.va);return false;
    }
    // These verified prologues contain only stack/register instructions.
    for(auto hook:hooks) {
        void *trampoline=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
        if(trampoline==MAP_FAILED) return false;
        memcpy(trampoline,hook.expected,16);
        NativeBridge::AbsoluteJump((char *)trampoline+16,(uintptr_t)EngineFunction<void *>(hook.va)+16);
        sys_icache_invalidate(trampoline,32);
        if(mprotect(trampoline,4096,PROT_READ|PROT_EXEC)) return false;
        *hook.original=trampoline;
        uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)hook.replacement);
        if(!NativeBridge::PatchCode(EngineFunction<void *>(hook.va),jump,16)) return false;
    }
    hooksInstalled=true;Log(@"native_pc_observers installed=1");return true;
}
static bool SetBool(void *object,const char *setter,bool value) {
    void *method=object?lookup(*(void **)object,setter,1,0):nullptr;if(!method) return false;
    void *exception=nullptr,*args[]={&value};invoke(method,object,args,&exception);
    return !exception;
}
static bool ownsMouseOverrides;
static uint64_t inputHandoffRevision;
static void ReleaseMouseOverrides(bool restoreVirtual) {
    if(!ownsMouseOverrides) return;
    ownsMouseOverrides=false;
    NativeBridge::SetDesktopMouseUI(false);
    NativeBridge::ReleaseKeys();
    char *owner=InputOwner();
    if(!owner || *(void **)(owner+GenshinOffsets().ownerModule)!=configuredModule ||
       *(void **)(owner+GenshinOffsets().ownerHybrid)!=configuredHybrid) return;
    SetBool(configuredModule,"set_allowTouchInput",oldAllowTouch);
    SetBool(configuredModule,"set_forceModuleActive",oldForce);
    // Restore only before the native switch. The controller's page handlers
    // own virtual-cursor state after that operation begins.
    if(restoreVirtual) SetBool(configuredHybrid,"set_useVirtualCursor",oldVirtual);
    Log(@"native_pc_released restore_virtual=%d",restoreVirtual);
}
static void BeforeInputSwitch(int mode) {
    if(mode!=3 && NativeBridge::stopControllerFeedback) NativeBridge::stopControllerFeedback();
    bool changed=NativeBridge::controllerInputActive.load()!=(mode==3);
    if(mode!=2) ReleaseMouseOverrides(true);
    if(changed) {
        ++inputHandoffRevision;
        NativeBridge::SetControllerOwnership(mode==3);
        // Drop ownership of old presses and motion, keeping the authoritative
        // hardware key-up source (including the accepted A-key repair).
        desktopEscapeGuard.reset();desktopNoticeEscapeOwned=false;
        desktopNativePressInContent=false;tabCursorRequested=false;
        ReleasePointerCapture();ReleaseFreePointerTracking();
        desktopDragSequence.reset();desktopMotionSequence.reset();desktopTouchHistory.reset();
        CGPoint point=DesktopMouseLocation();desktopMotion.reset({point.x,point.y});
    }
    if(changed && mode==2) {
        // Seed a usable physical pointer before rebuilding the native views.
        // Their subsequent Cursor requests own the final menu/world state.
        cameraCursorHidden=false;
        SetCursorVisible(true);SetCursorLockMode(0);
    }
}
static void Apply(void) {
    char *owner=InputOwner();if(!owner) return;
    int mode=*(int *)(owner+GenshinOffsets().ownerInputMode);
    NativeBridge::SetControllerOwnership(mode==3);
    if(mode!=2) {
        // The native operation owns all flags after a switch. A missed hook
        // drops our ownership without restoring stale page/module values.
        if(ownsMouseOverrides) {
            ownsMouseOverrides=false;NativeBridge::ReleaseKeys();
            Log(@"native_pc_late_release device=%d flags_untouched=1",mode);
        }
        NativeBridge::SetDesktopMouseUI(false);return;
    }
    void *module=*(void **)(owner+GenshinOffsets().ownerModule),*hybrid=*(void **)(owner+GenshinOffsets().ownerHybrid);
    if(!module || !hybrid || strcmp(className(*(void **)module),"MihoyoStandaloneInputModule") ||
       strcmp(className(*(void **)hybrid),"MihoyoHybridInput")) return;
    if(!wantNative) {ReleaseMouseOverrides(true);return;}
    if(module!=configuredModule || hybrid!=configuredHybrid) {
        ownsMouseOverrides=false;NativeBridge::SetDesktopMouseUI(false);
        configuredModule=module;configuredHybrid=hybrid;
    }
    if(!ownsMouseOverrides) {
        oldAllowTouch=Bool(module,"get_allowTouchInput");oldForce=Bool(module,"get_forceModuleActive");
        oldVirtual=Bool(hybrid,"get_useVirtualCursor");
        ownsMouseOverrides=true;
        Log(@"native_pc_original_flags allowTouch=%d force=%d virtual=%d",oldAllowTouch,oldForce,oldVirtual);
    }
    bool active=NativeBridge::nativeDesktopMouseUI.load();
    bool ok=SetBool(module,"set_allowTouchInput",false) && SetBool(module,"set_forceModuleActive",true) &&
        SetBool(hybrid,"set_useVirtualCursor",false);
    bool native=ok && !Bool(module,"get_allowTouchInput") && Bool(module,"get_forceModuleActive") &&
        !Bool(hybrid,"get_useVirtualCursor");
    NativeBridge::SetDesktopMouseUI(native);
    if(active!=native) Log(@"native_pc_enabled=%d allowTouch=%d force=%d virtual=%d mousePresent=%d",
        native,Bool(module,"get_allowTouchInput"),Bool(module,"get_forceModuleActive"),Bool(hybrid,"get_useVirtualCursor"),Bool(hybrid,"get_mousePresent"));
}
static bool RestoreKeyboardMouseDevices(void) {
    char *owner=InputOwner();
    if(!owner || *(int *)(owner+GenshinOffsets().ownerInputMode)!=2 ||
       !DesktopControlPreference::bootstrapReady.load()) return false;
    char *input=*(char **)(owner+GenshinOffsets().ownerGameInput);
    void *player=input?*(void **)(input+GenshinOffsets().inputPlayer):nullptr;
    void *controllers=player?*(void **)((char *)player+GenshinOffsets().playerControllers):nullptr;
    if(!controllers || strcmp(className(*(void **)controllers),"ControllerHelper")) return false;
    void *keyboard=Property(controllers,"get_Keyboard"),*mouse=Property(controllers,"get_Mouse");
    if(!keyboard || !mouse || strcmp(className(*(void **)keyboard),"Keyboard") ||
       strcmp(className(*(void **)mouse),"Mouse")) return false;
    // Bootstrap previously enabled these only once. The controller switch is
    // allowed to disable them; returning to PC must restore both, not just UI.
    if(!Bool(keyboard,"get_enabled")) EnableInputDevice(keyboard);
    if(!Bool(mouse,"get_enabled")) EnableInputDevice(mouse);
    if(keyboard!=rewiredKeyboard) InspectRewiredState();
    void *module=*(void **)(owner+GenshinOffsets().ownerModule);
    if(!module || strcmp(className(*(void **)module),"MihoyoStandaloneInputModule")) return false;
    if(!Bool(module,"get_enabled")) SetBool(module,"set_enabled",true);
    Apply();
    return Bool(keyboard,"get_enabled") && Bool(mouse,"get_enabled") &&
        Bool(module,"get_enabled") && NativeBridge::nativeDesktopMouseUI.load();
}
static void AfterInputSwitch(int mode) {
    uint64_t revision=++inputHandoffRevision;
    if(mode!=2 || !wantNative || !DesktopControlPreference::bootstrapReady.load()) return;
    bool ready=RestoreKeyboardMouseDevices();
    Log(@"native_pc_handoff input=2 keyboard_mouse_ready=%d source=committed_native_mode",ready);
    // Native settings/selection callbacks may finalize flags after the switch
    // returns. Reconcile twice without scans, per-frame work or a mode override.
    for(int delay:{50,250})
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)delay*NSEC_PER_MSEC),dispatch_get_main_queue(), ^{
            if(revision!=inputHandoffRevision) return;
            bool restored=RestoreKeyboardMouseDevices();
            RefreshPointerLock();
            Log(@"native_pc_handoff settled_ms=%d ready=%d",delay,restored);
        });
}
static void State(void) {
    char *owner=InputOwner();if(!owner) return;
    void *module=*(void **)(owner+GenshinOffsets().ownerModule),*hybrid=*(void **)(owner+GenshinOffsets().ownerHybrid),*eventSystem=*(void **)(owner+GenshinOffsets().ownerEventSystem);
    void *current=Property(eventSystem,"get_currentInputModule");
    id window=NativeGameWindow();
    NSUInteger style=window?((NSUInteger (*)(id,SEL))objc_msgSend)(window,sel_registerName("styleMask")):0;
    Log(@"native_pc_state input=%d module=%s hybrid=%s eventSystem=%s current=%s enabled=%d native=%d mouseFrames=%llu touchPasses=%llu touchConsumed=%llu",
        *(int *)(owner+GenshinOffsets().ownerInputMode),module?className(*(void **)module):"null",hybrid?className(*(void **)hybrid):"null",
        eventSystem?className(*(void **)eventSystem):"null",current?className(*(void **)current):"null",
        module?Bool(module,"get_enabled"):0,NativeBridge::nativeDesktopMouseUI.load(),mouseFrames,touchPasses,touchConsumed);
    Log(@"native_pc_window fullscreen=%d foreground=%d",(style&(1ULL<<14))!=0,NativeBridge::IsForeground());
    if(module && module!=previousModule) {
        previousModule=module;
        InspectInputObject(module,"native_pc_module");
        if(hybrid) InspectInputObject(hybrid,"native_pc_hybrid");
    }
}
static void Inspect(void) {
    if(!IsCompatibleRuntime()) return;
    for(const char *name:{"MihoyoStandaloneInputModule","MihoyoHybridInput","MihoyoEventSystem","MonoInLevelMapPage","InLevelMapPageContext"})
        Describe(findClass("Assembly-CSharp.dll","MoleMole",name));
    for(const char *name:{"EasyTouch","Finger","Gesture"})
        Describe(findClass("Assembly-CSharp-firstpass.dll","HedgehogTeam.EasyTouch",name));
    char *owner=InputOwner();
    if(owner) {
        void *easy=*(void **)(owner+80);
        if(easy) InspectInputObject(easy,"native_pc_easy_touch");
    }
    for(const char *name:{"RewiredStandaloneInputModule","RewiredPointerInputModule"})
        Describe(findClass("Assembly-CSharp-firstpass.dll","Rewired.Integration.UnityUI",name));
    for(const char *name:{"StandaloneInputModule","PointerInputModule","BaseInputModule","BaseInput"})
        Describe(findClass("UnityEngine.UI.dll","UnityEngine.EventSystems",name));
    State();inspected=true;
    Log(@"native_pc_inspection complete=1 behavior_changed=0");
}
static void Install(void) {
    if(!IsCompatibleRuntime()) return;
    DesktopUI::beforeInputSwitch=BeforeInputSwitch;
    DesktopUI::afterInputSwitch=AfterInputSwitch;
    // Optional pointer/touch observers only collect old-build diagnostics.
    // Native desktop input is configured through its real module properties.
    if(UsesLegacyMetadataLayout() && !InstallObservers()) return;
    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *) {
        if(!inspected) {if(diagnosticsEnabled && UsesLegacyMetadataLayout()) Inspect();else inspected=true;}
        NSString *path=[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/GenshinNativePCControl.json"];
        NSData *data=[NSData dataWithContentsOfFile:path];
        NSDictionary *control=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
        if([control isKindOfClass:NSDictionary.class]) {
            NSString *mode=control[@"mode"];
            if([mode isEqualToString:@"native"]) wantNative=true;
            else if([mode isEqualToString:@"revision16"]) wantNative=false;
        }
        Apply();
        static unsigned seconds=0;
        if(++seconds%10==0 && diagnosticsEnabled) State();
    }];
}
}
