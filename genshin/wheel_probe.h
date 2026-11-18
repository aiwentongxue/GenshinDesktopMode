namespace WheelProbe {
using namespace SceneUIProbe;
static void Fields(void *cls,void *object) {
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto fieldName=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
    auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
    auto offset=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    void *it=nullptr;
    while(auto field=(uintptr_t *)next(cls,&it)) {
        void *fc=fromType(ManagedFieldType(field));if(!fc) continue;
        const char *type=className(fc),*name=fieldName(field);unsigned off=offset(field);
        if(!object) {Log(@"wheel_field class=%s name=%s type=%s offset=%u",className(cls),name,type,off);continue;}
        if((flags(field)&0x10) || off<16 || off>4096) continue;
        char *p=(char *)object+off;
        if(!strcmp(type,"Single")) Log(@"wheel_value class=%s name=%s value=%.5f",className(cls),name,*(float *)p);
        else if(!strcmp(type,"Vector2")) Log(@"wheel_value class=%s name=%s value=(%.2f,%.2f)",className(cls),name,*(float *)p,*(float *)(p+4));
        else if(!strcmp(type,"Vector3")) Log(@"wheel_value class=%s name=%s value=(%.2f,%.2f,%.2f)",className(cls),name,*(float *)p,*(float *)(p+4),*(float *)(p+8));
        else if(!strcmp(type,"Int32")) Log(@"wheel_value class=%s name=%s value=%d",className(cls),name,*(int *)p);
    }
}
static void InspectInstances(void *cls) {
    void *type=EngineFunction<void *(*)(void *)>(0x10e9bb654)(cls);
    void *objectType=EngineFunction<void *(*)(void *)>(0x10e9bb7b8)(type);
    void *resources=findClass("UnityEngine.CoreModule.dll","UnityEngine","Resources");
    void *args[]={objectType};
    void *array=Call(lookup(resources,"FindObjectsOfTypeAll",1,0),nullptr,args);if(!array) return;
    uint64_t length=*(uint64_t *)((char *)array+24);if(length>500) return;
    for(uint64_t i=0;i<length;++i) {
        void *object=((void **)((char *)array+32))[i];
        void *go=Property(object,"get_gameObject");
        if(!go || !Bool(go,"get_activeInHierarchy")) continue;
        Log(@"wheel_active class=%s name=%@",className(cls),Name(go));
        Fields(cls,object);
    }
}
static void Install(void) {
    static std::vector<void *> classes;
    for(const char *name:{"MonoInLevelVirtualDialDialog","MonoVirtualDialItem","MonoBeyondInLevelVirtualDialDialog","MonoQuickSelectDialog"}) {
        void *cls=findClass("Assembly-CSharp.dll","MoleMole",name);
        Log(@"wheel_class name=%s found=%d",name,cls!=nullptr);
        if(!cls) continue;
        Signatures(cls);Fields(cls,nullptr);
        if(strncmp(name,"Mono",4)==0) classes.push_back(cls);
    }
    [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer) {
        static unsigned count=0;
        if(++count>240) {[timer invalidate];return;}
        if(!NativeBridge::IsKeyHeld(9)) return;
        for(void *cls:classes) InspectInstances(cls);
        float px,py;{std::lock_guard<std::mutex> guard(NativeBridge::mutex);px=NativeBridge::normalizedX;py=NativeBridge::normalizedY;}
        const auto [w,h]=NativeBridge::screenMetrics.read();
        Log(@"wheel_pointer normalized=(%.4f,%.4f) unity=(%.1f,%.1f) screen=(%d,%d)",px,py,
            *(float *)(nativeInputManager+0xd0),*(float *)(nativeInputManager+0xd4),
            w,h);
    }];
}
}
