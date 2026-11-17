// Resolve devices only after the real game input owner is alive. A fixed delay
// misses this step when the app stays inactive or loading takes longer.
static bool TryReadyDesktopDevices(void) {
    if(!IsCompatibleRuntime() || !NativeBridge::enabled || !NativeBridge::frames.load()) return false;
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    char *owner=VerifiedInputOwner();
    if(!owner || strcmp(className(*(void **)owner),InputOwnerClassName())) return false;
    char *input=*(char **)(owner+GenshinOffsets().ownerGameInput);
    if(!input || input!=NativeBridge::gameInput || !*(void **)(input+GenshinOffsets().inputManager)) return false;
    void *player=*(void **)(input+GenshinOffsets().inputPlayer);
    void *controllers=player?*(void **)((char *)player+GenshinOffsets().playerControllers):nullptr;
    if(!controllers || strcmp(className(*(void **)controllers),"ControllerHelper")) return false;
    if(!UsesLegacyMetadataLayout() && !HasManagedField(*(void **)player,"controllers",GenshinOffsets().playerControllers,"ControllerHelper")) return false;
    void *keyboard=ReadManagedProperty(controllers,"get_Keyboard");
    void *mouse=ReadManagedProperty(controllers,"get_Mouse");
    if(!keyboard || !mouse || strcmp(className(*(void **)keyboard),"Keyboard") ||
       strcmp(className(*(void **)mouse),"Mouse")) return false;

    void *controllerCount=ReadManagedProperty(controllers,"get_joystickCount");
    int joystickCount=controllerCount?*(int *)((char *)controllerCount+16):-1;
    if(!TryDesktopControlMode(joystickCount))return false;
    InspectRewiredState();
    void *keyboardEnabled=ReadManagedProperty(keyboard,"get_enabled",true);
    void *mouseEnabled=ReadManagedProperty(mouse,"get_enabled",true);
    int mode=*(int *)(owner+GenshinOffsets().ownerInputMode);
    bool ready=mode==3?joystickCount>0:
        keyboardEnabled && mouseEnabled && *(bool *)((char *)keyboardEnabled+16) &&
        *(bool *)((char *)mouseEnabled+16);
    Log(@"desktop_devices_ready ready=%d input=%d keyboard_reader=%d axis_reader=%d",
        ready,*(int *)(owner+GenshinOffsets().ownerInputMode),rewiredKeyReader!=nullptr,gameAxisReader!=nullptr);
    return ready;
}

static void InstallDeferredDesktopDevices(void) {
    if(TryReadyDesktopDevices()) return;
    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *timer) {
        if(TryReadyDesktopDevices()) [timer invalidate];
    }];
    Log(@"desktop_devices_waiting_for_runtime=1");
}
