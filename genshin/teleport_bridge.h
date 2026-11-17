namespace TeleportCursorBridge {
using namespace SceneUIProbe;
static void *mapType,*loadingType,*canvasType,*canvasGroupType;
static void *findObjectsMethod,*getComponentMethod;
static double nextMapScan=0;
static bool map=false;

static void *TypeObject(const char *assembly,const char *space,const char *name) {
    void *cls=findClass(assembly,space,name);if(!cls) return nullptr;
    void *type=EngineFunction<void *(*)(void *)>(0x10e9bb654)(cls);
    return EngineFunction<void *(*)(void *)>(0x10e9bb7b8)(type);
}
static void *OneTypeArgument(void *cls,const char *name) {
    if(!cls) return nullptr;
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto methodName=EngineFunction<const char *(*)(void *)>(0x10e9bb6f0);
    auto count=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    auto parameter=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    void *it=nullptr;
    while(void *method=next(cls,&it)) {
        if(strcmp(methodName(method),name) || count(method)!=1) continue;
        void *parameterClass=fromType(parameter(method,0));
        if(parameterClass && !strcmp(className(parameterClass),"Type")) return method;
    }
    return nullptr;
}
static bool Resolve(void) {
    if(mapType && loadingType && canvasType && canvasGroupType && findObjectsMethod && getComponentMethod) return true;
    mapType=TypeObject("Assembly-CSharp.dll","MoleMole","MonoInLevelMapPage");
    loadingType=TypeObject("Assembly-CSharp.dll","MoleMole","MonoLoadingCanvas");
    canvasType=TypeObject("UnityEngine.UIModule.dll","UnityEngine","Canvas");
    canvasGroupType=TypeObject("UnityEngine.UIModule.dll","UnityEngine","CanvasGroup");
    findObjectsMethod=OneTypeArgument(findClass("UnityEngine.CoreModule.dll","UnityEngine","Object"),"FindObjectsOfType");
    getComponentMethod=OneTypeArgument(findClass("UnityEngine.CoreModule.dll","UnityEngine","Component"),"GetComponent");
    static bool described=false;
    if(!described && mapType && loadingType) {
        described=true;
        Log(@"teleport_cursor_resolved map=%d loading=%d canvas=%d group=%d finder=%d component=%d",
            mapType!=nullptr,loadingType!=nullptr,canvasType!=nullptr,canvasGroupType!=nullptr,
            findObjectsMethod!=nullptr,getComponentMethod!=nullptr);
    }
    return mapType && loadingType && canvasType && canvasGroupType && findObjectsMethod && getComponentMethod;
}
static void *Find(void *type) {void *args[]={type};return Call(findObjectsMethod,nullptr,args);}
static void *Component(void *object,void *type) {void *args[]={type};return Call(getComponentMethod,object,args);}
static bool Active(void *object) {
    return object && Bool(Property(object,"get_gameObject"),"get_activeInHierarchy") && Bool(object,"get_isActiveAndEnabled");
}
static bool MapOpen(void) {
    if(NativeBridge::mapContextReader) {
        int visible=NativeBridge::mapContextReader();
        if(visible>=0) return visible!=0;
    }
    void *array=Find(mapType);if(!array) return false;
    uint64_t count=*(uint64_t *)((char *)array+24);if(count>32) return false;
    for(uint64_t i=0;i<count;++i)
        if(Active(((void **)((char *)array+32))[i])) return true;
    return false;
}
static bool MapTouchContext(void) {
    map=Resolve() && MapOpen();
    nextMapScan=NSProcessInfo.processInfo.systemUptime+0.20;
    NativeBridge::mapPageVisible=map;
    return map;
}
static bool PaimonScrollContext(void) {
    static void *types[2];static bool resolved=false,visible=false;
    static double nextCheck=-1;
    double now=NSProcessInfo.processInfo.systemUptime;
    if(now<nextCheck) return visible;
    nextCheck=now+0.20;
    if(!Resolve()) return false;
    if(!resolved) {
        resolved=true;
        types[0]=TypeObject("Assembly-CSharp.dll","MoleMole","MonoInLevelPlayerProfilePage");
        types[1]=TypeObject("Assembly-CSharp.dll","MoleMole","MonoInLevelPlayerProfilePageV3");
    }
    bool active=false;
    for(void *type:types) {
        if(!type) continue;
        void *array=Find(type);if(!array) continue;
        uint64_t count=*(uint64_t *)((char *)array+24);if(count>16) continue;
        for(uint64_t i=0;i<count;++i)
            if(Active(((void **)((char *)array+32))[i])) {active=true;break;}
        if(active) break;
    }
    if(visible!=active) Log(@"paimon_scroll_context visible=%d mos_gain=4 raw_gain=1",active);
    visible=active;return visible;
}
// Resolve only on visible-UI wheel input, with the same bounded cache as
// the Paimon list. Shared managed type names cover both regional clients.
static bool SettingsScrollContext(void) {
    static void *type;static bool visible=false;static double nextCheck=-1;
    double now=NSProcessInfo.processInfo.systemUptime;
    if(now<nextCheck)return visible;
    nextCheck=now+0.20;
    if(!Resolve())return false;
    if(!type)type=TypeObject("Assembly-CSharp.dll","MoleMole","MonoSettingPage");
    bool active=false;
    if(type) {
        void *array=Find(type);
        uint64_t count=array?*(uint64_t *)((char *)array+24):0;
        if(count<=16)for(uint64_t i=0;i<count;++i)
            if(Active(((void **)((char *)array+32))[i])) {active=true;break;}
    }
    if(visible!=active)Log(@"settings_scroll_context visible=%d mos_gain=4 raw_gain=1.5",active);
    visible=active;return visible;
}
static bool FriendScrollContext(void) {
    static void *type;static bool visible=false;static double nextCheck=-1;
    double now=NSProcessInfo.processInfo.systemUptime;
    if(now<nextCheck)return visible;nextCheck=now+.20;
    if(!Resolve())return false;
    if(!type)type=TypeObject("Assembly-CSharp.dll","MoleMole","MonoFriendPage");
    bool active=false;
    if(type){void *array=Find(type);uint64_t count=array?*(uint64_t *)((char *)array+24):0;
        if(count<=16)for(uint64_t i=0;i<count;++i)if(Active(((void **)((char *)array+32))[i])){active=true;break;}}
    if(visible!=active)Log(@"friend_scroll_context visible=%d mos_gain=4 raw_gain=1.5",active);
    visible=active;return visible;
}
static bool LoadingVisible(void) {
    void *array=Find(loadingType);if(!array) return false;
    uint64_t count=*(uint64_t *)((char *)array+24);if(count>32) return false;
    for(uint64_t i=0;i<count;++i) {
        void *loading=((void **)((char *)array+32))[i];if(!Active(loading)) continue;
        void *transform=Property(loading,"get_transform");
        bool haveCanvas=false,visible=true;
        float alpha=1;
        for(unsigned depth=0;transform && depth<12;++depth) {
            if(void *canvas=Component(transform,canvasType)) {
                haveCanvas=true;
                if(!Bool(canvas,"get_isActiveAndEnabled")) visible=false;
            }
            if(void *group=Component(transform,canvasGroupType)) {
                void *value=Property(group,"get_alpha");
                if(value) alpha*=*(float *)((char *)value+16);
            }
            transform=Property(transform,"get_parent");
        }
        if(haveCanvas && visible && alpha>0.02f) return true;
    }
    return false;
}
static void Tick(void) {
    if(!NativeBridge::enabled || !NativeBridge::frames.load() || !Resolve()) return;
    const double now=NSProcessInfo.processInfo.systemUptime;
    bool rawVisible=cursorVisible.load() && !NativeBridge::nativeOverlayActive;
    // Never walk the scene in ordinary camera gameplay or while inactive.
    // A visible map arms the fast loading check; other menus use a slower
    // map query. The loading transition retains the previous 50ms cadence.
    if(!NativeBridge::IsForeground() && !teleportCursorPolicy.hidden) return;
    if(rawVisible && now>=nextMapScan) {map=MapOpen();nextMapScan=now+0.20;}
    if(!rawVisible && !teleportCursorPolicy.hidden) map=false;
    NativeBridge::mapPageVisible=map;
    bool armed=map || teleportCursorPolicy.hidden ||
        (teleportCursorPolicy.seenMap && now-teleportCursorPolicy.lastMapTime<=3.0);
    // A captured map pan cannot activate Teleport. Scan for loading again on
    // release; avoid scene-wide reflection during a sustained mouse drag.
    bool loading=armed&&!NativeBridge::mapDragActive.load()?LoadingVisible():false;
    bool changed=teleportCursorPolicy.sample(map,loading,cursorVisible.load(),cursorShowRevision.load(),
        tabCursorRequested || NativeBridge::IsKeyHeld(9),now);
    static bool first=true,previousMap=false,previousLoading=false;
    if(first || map!=previousMap || loading!=previousLoading || changed) {
        Log(@"teleport_cursor_state map=%d loading=%d hide=%d game_visible=%d",
            map,loading,teleportCursorPolicy.hidden,cursorVisible.load());
        first=false;previousMap=map;previousLoading=loading;
    }
    if(changed) RefreshPointerStateNow();
}
static void Install(void) {
    NativeBridge::paimonScrollContext=PaimonScrollContext;
    NativeBridge::settingsScrollContext=SettingsScrollContext;
    NativeBridge::friendScrollContext=FriendScrollContext;
    [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *) {Tick();}];
    Log(@"teleport_cursor_monitor installed=1 source=map_and_loading_canvas");
}
}
