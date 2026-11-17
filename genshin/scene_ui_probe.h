namespace SceneUIProbe {
static auto lookup=EngineFunction<void *(*)(void *,const char *,int,int)>(0x10f0572e8);
static auto invoke=EngineFunction<void *(*)(void *,void *,void **,void **)>(0x10e9bb768);
static auto findClass=EngineFunction<void *(*)(const char *,const char *,const char *)>(0x10f057bd4);
static auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
static auto classSpace=EngineFunction<const char *(*)(void *)>(0x10e9bb604);
static void *componentType;

static void *Call(void *method,void *object,void **args=nullptr) {
    if(!method) return nullptr;
    void *exception=nullptr,*result=invoke(method,object,args,&exception);
    if(exception) {Log(@"scene_ui managed_exception=%s",className(*(void **)exception));return nullptr;}
    return result;
}
static void *Property(void *object,const char *name) {
    return object?Call(lookup(*(void **)object,name,0,0),object):nullptr;
}
static NSString *String(void *str) {
    if(!str) return @"";
    int length=*(int *)((char *)str+16);
    if(length<0 || length>4096) return @"<invalid-name>";
    return [NSString stringWithCharacters:(const unichar *)((char *)str+20) length:length];
}
static NSString *Name(void *object) {return String(Property(object,"get_name"));}
static bool Bool(void *object,const char *name) {
    void *value=Property(object,name);return value && *(bool *)((char *)value+16);
}
static void *Type(const char *name) {
    void *cls=findClass("UnityEngine.CoreModule.dll","UnityEngine",name);
    if(!cls) {Log(@"scene_ui missing_class=%s",name);return nullptr;}
    void *type=EngineFunction<void *(*)(void *)>(0x10e9bb654)(cls);
    if(EngineFunction<void *(*)(void *)>(0x10e9bb5e0)(type)!=cls) {
        Log(@"scene_ui invalid_type_roundtrip=%s",name);return nullptr;
    }
    void *object=EngineFunction<void *(*)(void *)>(0x10e9bb7b8)(type);
    Log(@"scene_ui reflected_type=%s class=%s",name,object?className(*(void **)object):"null");
    return object;
}
static void Signatures(void *cls) {
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto methodName=EngineFunction<const char *(*)(void *)>(0x10e9bb6f0);
    auto retType=EngineFunction<void *(*)(void *)>(0x10e9bb6e8);
    auto paramType=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto paramCount=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    void *it=nullptr;
    while(void *method=next(cls,&it)) {
        void *rc=fromType(retType(method));NSMutableString *params=[NSMutableString string];
        for(unsigned i=0;i<paramCount(method);++i) {
            void *pc=fromType(paramType(method,i));[params appendFormat:@"%s%s",i?",":"",pc?className(pc):"?"];
        }
        Log(@"scene_ui_method class=%s name=%s return=%s params=%@ code=0x%llx",
            className(cls),methodName(method),rc?className(rc):"?",params,ManagedMethodCode(method)-_dyld_get_image_vmaddr_slide(0));
    }
}
static void Components(void *object,bool detailed) {
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto methodName=EngineFunction<const char *(*)(void *)>(0x10e9bb6f0);
    auto count=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    auto paramType=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    void *it=nullptr,*method=nullptr;
    while(void *m=next(*(void **)object,&it)) {
        if(strcmp(methodName(m),"GetComponents") || count(m)!=1) continue;
        void *pc=fromType(paramType(m,0));
        if(pc && !strcmp(className(pc),"Type")) {method=m;break;}
    }
    void *args[]={componentType},*array=Call(method,object,args);if(!array) return;
    uint64_t length=*(uint64_t *)((char *)array+24);if(length>100) return;
    for(uint64_t i=0;i<length;++i) {
        void *component=((void **)((char *)array+32))[i];if(!component) continue;
        void *cls=*(void **)component;
        Log(@"scene_ui_component object=%@ type=%s.%s",Name(object),classSpace(cls),className(cls));
        if(detailed && !strncmp(classSpace(cls),"Unity",5)) continue;
        if(detailed) {InspectInputObject(component,className(cls));Signatures(cls);}
    }
}
static void Tree(void *transform,unsigned depth,unsigned &budget) {
    if(!transform || depth>18 || !budget) return;
    --budget;
    void *object=Property(transform,"get_gameObject");if(!object) return;
    NSString *name=Name(object);
    Log(@"scene_ui_node depth=%u active=%d name=%@",depth,Bool(object,"get_activeSelf"),name);
    if(depth<=1 || [name containsString:@"PC"] || [name containsString:@"Mobile"] || [name containsString:@"Keyboard"])
        Components(object,depth==0);
    void *value=Property(transform,"get_childCount");int count=value?*(int *)((char *)value+16):0;
    if(count<0 || count>2000) return;
    void *method=lookup(*(void **)transform,"GetChild",1,0);
    for(int i=0;i<count && budget;++i) {void *args[]={&i};Tree(Call(method,transform,args),depth+1,budget);}
}
static bool Inspect(void) {
    static void *gameObjectType=Type("GameObject");
    if(!componentType) componentType=Type("Component");
    if(!gameObjectType || !componentType) {Log(@"scene_ui missing_reflection_type");return false;}
    void *resources=findClass("UnityEngine.CoreModule.dll","UnityEngine","Resources");
    if(!resources) return false;
    void *args[]={gameObjectType};
    void *array=Call(lookup(resources,"FindObjectsOfTypeAll",1,0),nullptr,args);
    if(!array) {Log(@"scene_ui missing_object_array");return false;}
    uint64_t length=*(uint64_t *)((char *)array+24);if(length>500000) return false;
    unsigned roots=0,candidates=0;
    for(uint64_t i=0;i<length;++i) {
        void *object=((void **)((char *)array+32))[i];if(!object) continue;
        NSString *name=Name(object);
        if([name containsString:@"InLevelMain"] || [name containsString:@"MainPage"] || [name containsString:@"KeyboardUI"] || [name isEqualToString:@"Canvas"]) {
            bool active=Bool(object,"get_activeInHierarchy");
            if(candidates++<80) Log(@"scene_ui_candidate name=%@ active=%d",name,active);
            if(active && [name containsString:@"InLevelMain"] && roots<3) {
                ++roots;unsigned budget=1800;Tree(Property(object,"get_transform"),0,budget);
            }
        }
    }
    Log(@"scene_ui_scan objects=%llu candidates=%u roots=%u",length,candidates,roots);
    return roots>0;
}
static void Install(void) {
    static bool done=false;static unsigned attempts=0;
    [NSTimer scheduledTimerWithTimeInterval:10 repeats:YES block:^(NSTimer *timer) {
        if(done || ++attempts>24) {[timer invalidate];return;}
        done=Inspect();
    }];
}
}
