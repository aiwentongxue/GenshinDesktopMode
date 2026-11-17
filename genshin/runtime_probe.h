// Engine entry points and data layouts must match a verified code profile.
// Region, CFBundle version, and signing/repacking UUID are not compatibility gates.
#include "runtime_layouts.h"
#include "runtime_discovery_image.h"
static uintptr_t runtimeLayoutMismatch;
static const uint8_t *RuntimeImageRange(uintptr_t address,size_t length,bool writable) {
    const auto *header=(const mach_header_64 *)_dyld_get_image_header(0);
    if(!header || header->magic!=MH_MAGIC_64 || header->ncmds>1024 || !length) return nullptr;
    const auto *command=(const load_command *)(header+1);
    const char *end=(const char *)command+header->sizeofcmds;
    for(unsigned i=0;i<header->ncmds;++i) {
        if((const char *)command+sizeof(load_command)>end || command->cmdsize<sizeof(load_command) ||
           command->cmdsize>(size_t)(end-(const char *)command)) return nullptr;
        if(command->cmd==LC_SEGMENT_64 && command->cmdsize>=sizeof(segment_command_64)) {
            const auto *segment=(const segment_command_64 *)command;
            int required=writable?(VM_PROT_READ|VM_PROT_WRITE):(VM_PROT_READ|VM_PROT_EXECUTE);
            if((segment->initprot&required)==required && address>=segment->vmaddr &&
               address-segment->vmaddr<=segment->vmsize && length<=segment->vmsize-(address-segment->vmaddr))
                return (const uint8_t *)(address+_dyld_get_image_vmaddr_slide(0));
        }
        command=(const load_command *)((const char *)command+command->cmdsize);
    }
    return nullptr;
}
static const GenshinRuntimeDiscovery::Bindings &ActiveRuntimeBindings(void) {
    // Cache the decision before our own hooks replace any profiled prologues.
    static const auto bindings=[] {
        const auto *header=(const mach_header_64 *)_dyld_get_image_header(0);
        std::vector<GenshinRuntimeDiscovery::CodeSpan> spans;
        if(!header || header->sizeofcmds>1024*1024 || !GenshinRuntimeDiscovery::CodeSpans(
                header,sizeof(*header)+header->sizeofcmds,RuntimeImageRange,spans))
            return GenshinRuntimeDiscovery::Bindings{};
        return GenshinRuntimeDiscovery::Select(RuntimeImageRange,spans.data(),spans.size(),runtimeLayoutMismatch);
    }();
    return bindings;
}
static GenshinRuntimeLayout ActiveRuntimeLayout(void) {return ActiveRuntimeBindings().layout;}
static bool IsCompatibleRuntime(void) {return ActiveRuntimeLayout()!=GenshinRuntimeLayout::Unsupported;}
static bool IsGlobalModernRuntime(void) {return ActiveRuntimeLayout()==GenshinRuntimeLayout::GlobalModernInput;}
static bool IsGlobalRuntime(void) {return ActiveRuntimeLayout()==GenshinRuntimeLayout::GlobalInput||IsGlobalModernRuntime();}
static bool IsModernRuntime(void) {return GenshinModernKeyboardPolicy(ActiveRuntimeLayout());}
static bool UsesLegacyMetadataLayout(void) {return ActiveRuntimeLayout()==GenshinRuntimeLayout::MainlandInput;}
static const char *RuntimeProfileName(void) {
    switch(ActiveRuntimeLayout()) {
        case GenshinRuntimeLayout::MainlandInput:return "mainland_legacy";
        case GenshinRuntimeLayout::GlobalInput:return "global_legacy";
        case GenshinRuntimeLayout::ModernInput:return "modern_input";
        case GenshinRuntimeLayout::GlobalModernInput:return "global_modern_input";
        default:return "unsupported";
    }
}
static const GenshinManagedOffsets &GenshinOffsets(void) {
    if(IsGlobalModernRuntime()) return GenshinGlobalModernOffsets;
    if(IsModernRuntime()) return GenshinModernOffsets;
    return IsGlobalRuntime()?GenshinGlobalOffsets:GenshinMainlandOffsets;
}
static const char *InputOwnerClassName(void) {return IsModernRuntime()?"MHCNPJOGAGH":"AADJAFKNJKO";}
static const char *GameInputClassName(void) {return IsModernRuntime()?"JGAPEPKBJJP":"ELKFCMHKCNI";}
static const char *UIOwnerClassName(void) {return IsModernRuntime()?"BOMCNPDGJOP":"DAEMCCNOCPF";}
static const char *InputModeClassName(void) {return IsModernRuntime()?"HBPCBEIMOCC":"EKAEMFGFEMJ";}
static const char *UILayoutClassName(void) {return IsModernRuntime()?"KIJKMLMBKCB":"MBGAPCMOGML";}
static const char *ControlSwitchMethodName(void) {return IsModernRuntime()?"KDGDBHJKJBI":"PLCNIMKNPBL";}
static const char *LayoutSwitchMethodName(void) {return IsModernRuntime()?"IGKCDAAKAOI":"FDDFPNEHHEH";}

template<typename T> static T EngineFunction(uintptr_t va) {
    uintptr_t actual=GenshinRuntimeDiscovery::Resolve(ActiveRuntimeBindings(),va);
    return actual?(T)(actual+_dyld_get_image_vmaddr_slide(0)):nullptr;
}
static bool RuntimePrologueMatches(uintptr_t va,const void *target,size_t length=16) {
    const auto *pattern=GenshinExpectedPattern(ActiveRuntimeLayout(),va);
    return target && pattern && length<=sizeof(pattern->code) && !memcmp(target,pattern->code,length);
}
static uintptr_t ManagedMethodCode(void *method) {
    if(!method || !IsCompatibleRuntime()) return 0;
    // IL2CPP moved the native entry from word 1 to word 0. Select by the
    // verified runtime ABI, then require executable memory in the main image.
    uintptr_t code=((const uintptr_t *)method)[GenshinMethodCodeWord(ActiveRuntimeLayout())];
    uintptr_t slide=_dyld_get_image_vmaddr_slide(0);
    return code>=slide && RuntimeImageRange(code-slide,4,false)?code:0;
}

// Use the runtime API: the two clients encode FieldInfo differently.
static void *ManagedFieldType(void *field) {
    return EngineFunction<void *(*)(void *)>(0x10e9bb6a0)(field);
}
static bool HasManagedField(void *cls,const char *name,size_t expectedOffset,const char *expectedType) {
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto fieldName=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
    auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
    auto offset=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);
    auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    for(unsigned level=0;cls && level<32;cls=parent(cls),++level) {
        void *it=nullptr;
        for(unsigned i=0;i<4096;++i) {
            void *field=next(cls,&it);if(!field) break;
            if((flags(field)&0x10) || offset(field)!=expectedOffset || (name && strcmp(fieldName(field),name))) continue;
            void *type=fromType(ManagedFieldType(field));
            if(type && !strcmp(className(type),expectedType)) return true;
        }
    }
    return false;
}
static void *ManagedFieldClassAt(void *cls,size_t expectedOffset,const char *expectedType) {
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
    auto offset=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);
    auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    auto name=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    for(unsigned depth=0;cls&&depth<32;cls=parent(cls),++depth) {
        void *it=nullptr;
        while(void *field=next(cls,&it)) {
            if((flags(field)&0x10)||offset(field)!=expectedOffset) continue;
            void *type=fromType(ManagedFieldType(field));
            if(type&&!strcmp(name(type),expectedType)) return type;
        }
    }
    return nullptr;
}
static bool VerifyDesktopManagedLayout(void *ownerClass) {
    if(UsesLegacyMetadataLayout()) return true;
    static void *verifiedClass;
    if(ownerClass==verifiedClass) return true;
    auto findClass=EngineFunction<void *(*)(const char *,const char *,const char *)>(0x10f057bd4);
    const auto &o=GenshinOffsets();
    struct Field {size_t offset;const char *type;};
    const Field fields[]={
        {o.ownerEventSystem,"MihoyoEventSystem"},
        {o.ownerModule,"MihoyoStandaloneInputModule"},
        {o.ownerHybrid,"MihoyoHybridInput"},
        {o.ownerGameInput,GameInputClassName()},
        {o.ownerInputMode,InputModeClassName()}};
    for(auto f:fields) if(!HasManagedField(ownerClass,nullptr,f.offset,f.type)) return false;
    void *input=findClass("Assembly-CSharp.dll","",GameInputClassName());
    void *ui=findClass("Assembly-CSharp.dll","",UIOwnerClassName());
    if(IsGlobalModernRuntime()) {
        auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
        if(!input) input=ManagedFieldClassAt(ownerClass,o.ownerGameInput,GameInputClassName());
        if(!ui) {
            char *statics=*EngineFunction<char **>(0x115644a70);
            char *object=statics?*(char **)(statics+o.uiStatic):nullptr;
            if(object&&!strcmp(className(*(void **)object),UIOwnerClassName())) ui=*(void **)object;
        }
    }
    if(!HasManagedField(input,nullptr,o.inputPlayer,"Player") ||
       !HasManagedField(input,nullptr,o.inputManager,"InputManager") ||
       !HasManagedField(ui,nullptr,o.uiLayout,UILayoutClassName())) return false;
    verifiedClass=ownerClass;
    Log(@"desktop_managed_layout verified=1 profile=%s fields=8",RuntimeProfileName());return true;
}
static char *VerifiedInputOwner(void) {
    if(!IsCompatibleRuntime()) return nullptr;
    char *statics=*EngineFunction<char **>(0x115644a70);
    char *owner=statics?*(char **)(statics+GenshinOffsets().ownerStatic):nullptr;
    if(!owner) return nullptr;
    auto findClass=EngineFunction<void *(*)(const char *,const char *,const char *)>(0x10f057bd4);
    void *expected=findClass("Assembly-CSharp.dll","",InputOwnerClassName());
    if(!expected&&IsGlobalModernRuntime()) {
        auto name=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
        if(!strcmp(name(*(void **)owner),InputOwnerClassName())) expected=*(void **)owner;
    }
    return expected && *(void **)owner==expected && VerifyDesktopManagedLayout(expected)?owner:nullptr;
}
static char *VerifiedUIOwner(void) {
    if(!IsCompatibleRuntime()) return nullptr;
    char *statics=*EngineFunction<char **>(0x115644a70);
    char *owner=statics?*(char **)(statics+GenshinOffsets().uiStatic):nullptr;
    if(!owner) return nullptr;
    auto findClass=EngineFunction<void *(*)(const char *,const char *,const char *)>(0x10f057bd4);
    void *expected=findClass("Assembly-CSharp.dll","",UIOwnerClassName());
    if(!expected&&IsGlobalModernRuntime()) {
        auto name=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
        if(!strcmp(name(*(void **)owner),UIOwnerClassName())) expected=*(void **)owner;
    }
    return expected && *(void **)owner==expected &&
        HasManagedField(expected,nullptr,GenshinOffsets().uiLayout,UILayoutClassName())?owner:nullptr;
}

static void InspectMethods(void *cls,const char *space,const char *name) {
    auto nextMethod=EngineFunction<void *(*)(void *, void **)>(0x10e9bb5f8);
    auto methodName=EngineFunction<const char *(*)(void *)>(0x10e9bb6f0);
    auto paramCount=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    if (!cls) return;
    void *iterator=nullptr;
    for (unsigned count=0;count<2000;++count) {
        void *method=nextMethod(cls,&iterator);
        if (!method) break;
        auto words=(uintptr_t *)method;
        Log(@"managed_method %s.%s name=%s args=%u info=%p w0=%p w1=%p w2=%p w3=%p w4=%p w5=%p",
            space,name,methodName(method),paramCount(method),method,
            (void *)words[0],(void *)words[1],(void *)words[2],(void *)words[3],
            (void *)words[4],(void *)words[5]);
    }
}

static void InspectManagedClass(const char *assembly, const char *space, const char *name) {
    auto findClass=EngineFunction<void *(*)(const char *, const char *, const char *)>(0x10f057bd4);
    void *cls=findClass(assembly,space,name);
    Log(@"managed_class [%s]%s.%s pointer=%p",assembly,space,name,cls);
    InspectMethods(cls,space,name);
}

static void InspectGameInputTypes(void) {
    if(!UsesLegacyMetadataLayout()) return; // This scanner belongs to the legacy metadata ABI.
    auto manager=EngineFunction<char *(*)()>(0x10f05b6fc)();
    auto images=*(char ***)(manager+0x208);
    uint64_t imageCount=(*(char ***)(manager+0x210)-images);
    Log(@"runtime_images count=%llu meta=%llx",imageCount,*(uint64_t *)(manager+0x210));
    if (imageCount<58 || imageCount>256) imageCount=58;
    auto findClass=EngineFunction<void *(*)(void *,const char *,const char *)>(0x10e9bb5e8);
    auto typeDef=EngineFunction<char *(*)(uint32_t)>(0x10ea0e980);
    auto metadataString=EngineFunction<const char *(*)(uint32_t)>(0x10ea0bd64);
    auto typeClass=EngineFunction<void *(*)(uint32_t)>(0x10ea0c3b4);
    for (unsigned i=0;i<imageCount;++i) {
        char *img=images[i];
        if (!img) continue;
        uint32_t count=*(uint32_t *)(img+0x30)+0x9bd93914u;
        uint32_t start=*(uint32_t *)(img+0x48)^0x79f2e891u;
        Log(@"runtime_image index=%u pointer=%p types=%u start=%u",i,img,count,start);
        for (const char *name : {"InputManager","UIUtils","Keyboard","PlatformUtils"}) {
            void *cls=findClass(img,"MoleMole",name);
            if (cls) { Log(@"game_input_class image=%u name=%s ptr=%p",i,name,cls);InspectMethods(cls,"MoleMole",name); }
        }
        if (count<10000 || count>300000) continue;
        for (uint32_t j=0;j<count;++j) {
            char *def=typeDef(start+j);
            if (!def) continue;
            const char *name=metadataString(*(uint32_t *)(def+8)^0x39fb23c8u);
            const char *space=metadataString(*(uint32_t *)(def+0x2c)^0x325f2c88u);
            if (!name || !space) continue;
            if (j<3) Log(@"runtime_type_sample image=%u index=%u name=%s.%s",i,start+j,space,name);
            if (strstr(name,"InputManager") || strstr(name,"UIUtils") || !strcmp(name,"Keyboard") ||
                strstr(name,"PlatformUtils") || !strcmp(name,"InputDevice")) {
                Log(@"runtime_input_type image=%u index=%u name=%s.%s",i,start+j,space,name);
                InspectMethods(typeClass(start+j),space,name);
            }
        }
    }
}

static void InspectInputFields(void) {
    auto findClass=EngineFunction<void *(*)(const char *,const char *,const char *)>(0x10f057bd4);
    auto nextField=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto fieldName=EngineFunction<const char *(*)(void *)>(0x10ea03664);
    for (const char *name : {"GlobalVars","GeneralData"}) {
        void *cls=findClass("Assembly-CSharp-firstpass.dll","MoleMole",name);
        if (!cls) continue;
        void *iterator=nullptr;
        for (unsigned count=0;count<3000;++count) {
            void *field=nextField(cls,&iterator);if (!field) break;
            const char *n=fieldName(field);
            if (strcasestr(n,"input") || strcasestr(n,"keyboard") || strcasestr(n,"platform") ||
                strcasestr(n,"mouse") || strcasestr(n,"pcmode")) {
                auto w=(uintptr_t *)field;
                Log(@"input_field class=%s name=%s ptr=%p w0=%p w1=%p w2=%p w3=%p",name,n,field,
                    (void *)w[0],(void *)w[1],(void *)w[2],(void *)w[3]);
            }
        }
    }
}

static void ScanRewiredOwners(void) {
    if(!UsesLegacyMetadataLayout()) return; // This scanner belongs to the legacy metadata ABI.
    auto manager=EngineFunction<char *(*)()>(0x10f05b6fc)();
    auto images=*(char ***)(manager+0x208);
    char *img=images[60];
    if (!img) return;
    uint32_t count=*(uint32_t *)(img+0x30)+0x9bd93914u;
    uint32_t start=*(uint32_t *)(img+0x48)^0x79f2e891u;
    if (count>300000) return;
    auto typeDef=EngineFunction<char *(*)(uint32_t)>(0x10ea0e980);
    auto typeClass=EngineFunction<void *(*)(uint32_t)>(0x10ea0c3b4);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    auto classSpace=EngineFunction<const char *(*)(void *)>(0x10e9bb604);
    auto nextField=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto fieldName=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
    auto fieldOffset=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);
    unsigned inspected=0,candidates=0;
    Log(@"rewired_owner_scan begin types=%u",count);
    for (uint32_t j=0;j<count;++j) {
        char *def=typeDef(start+j);
        if (!def) continue;
        unsigned methodCount=*(uint16_t *)(def+0x32)^0xe0a2u;
        unsigned fieldCount=(uint16_t)(*(uint16_t *)(def+0x30)-0x790eu);
        if (methodCount<20 || fieldCount<6) continue;
        void *cls=typeClass(start+j);
        if (!cls) continue;
        ++inspected;bool candidate=false;void *iterator=nullptr;
        for (unsigned k=0;k<fieldCount;++k) {
            auto field=(uintptr_t *)nextField(cls,&iterator);if (!field) break;
            void *fieldType=ManagedFieldType(field);
            void *fieldClass=fromType(fieldType);if (!fieldClass) continue;
            const char *space=classSpace(fieldClass),*name=className(fieldClass);
            if (space && name && !strncmp(space,"Rewired",7)) {
                Log(@"rewired_owner index=%u class=%s field=%s field_type=%s.%s offset=%u methods=%u",
                    start+j,className(cls),fieldName(field),space,name,fieldOffset(field),methodCount);
                candidate=true;
            }
        }
        if (candidate) {++candidates;InspectMethods(cls,classSpace(cls),className(cls));}
    }
    Log(@"rewired_owner_scan end inspected=%u candidates=%u",inspected,candidates);
}

static void InspectInputManagerDetails(uint32_t index=13448) {
    if(!UsesLegacyMetadataLayout()) return; // This scanner belongs to the legacy metadata ABI.
    auto typeClass=EngineFunction<void *(*)(uint32_t)>(0x10ea0c3b4);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
    auto nextField=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto fieldName=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
    auto fieldFlags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
    auto fieldOffset=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);
    auto staticValue=EngineFunction<void (*)(void *,void *)>(0x10ea03c4c);
    void *cls=typeClass(index),*object=nullptr;
    if (!cls) return;
    for (void *p=cls;p;p=parent(p)) {
        Log(@"input_parent name=%s ptr=%p",className(p),p);
        void *iterator=nullptr;
        while (auto field=(uintptr_t *)nextField(p,&iterator)) {
            void *fc=fromType(ManagedFieldType(field));
            if (!fc) continue;
            unsigned flags=fieldFlags(field);
            Log(@"input_manager_field owner=%s name=%s type=%s offset=%u flags=%x",
                className(p),fieldName(field),className(fc),fieldOffset(field),flags);
            if ((flags&0x10) && !(flags&0x40) && fc==cls) {
                staticValue(field,&object);
                Log(@"input_manager_instance pointer=%p",object);
            }
            void *fp=parent(fc);
            if (fp && !strcmp(className(fp),"Enum")) {
                void *ei=nullptr;
                while (void *ef=nextField(fc,&ei)) {
                    if (!(fieldFlags(ef)&0x40)) continue;
                    uint64_t value=0;staticValue(ef,&value);
                    Log(@"input_enum type=%s member=%s value=%llu",className(fc),fieldName(ef),value);
                }
            }
        }
    }
    auto nextMethod=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto methodName=EngineFunction<const char *(*)(void *)>(0x10e9bb6f0);
    auto retType=EngineFunction<void *(*)(void *)>(0x10e9bb6e8);
    auto paramType=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto paramCount=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    void *iterator=nullptr;
    while (void *method=nextMethod(cls,&iterator)) {
        void *rc=fromType(retType(method));
        NSMutableString *params=[NSMutableString string];
        for (unsigned i=0;i<paramCount(method);++i) {
            void *pc=fromType(paramType(method,i));
            [params appendFormat:@"%s%s",i?",":"",pc?className(pc):"?"];
        }
        Log(@"input_signature name=%s static=%d return=%s params=%@ code=0x%llx info=%p",methodName(method),
            ((*(uint16_t *)((char *)method+GenshinMethodFlagsOffset(ActiveRuntimeLayout())))&0x10)!=0,rc?className(rc):"?",params,
            ManagedMethodCode(method)-_dyld_get_image_vmaddr_slide(0),method);
    }
}

static bool TryDesktopControlMode(int joystickCount) {
    if(DesktopControlPreference::bootstrapReady.load())return true;
    if (!IsCompatibleRuntime()) return false;
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    auto lookup=EngineFunction<void *(*)(void *,const char *,int,int)>(0x10f0572e8);
    auto invoke=EngineFunction<void *(*)(void *,void *,void **,void **)>(0x10e9bb768);
    char *owner=VerifiedInputOwner();
    if (!owner || strcmp(className(*(void **)owner),InputOwnerClassName())) {
        Log(@"desktop_control refused_missing_owner");return false;
    }
    char *input=*(char **)(owner+GenshinOffsets().ownerGameInput);
    int before=*(int *)(owner+GenshinOffsets().ownerInputMode);
    Log(@"desktop_control owner=%p before=%d input=%p",owner,before,input);
    if (!input || !*(void **)(input+GenshinOffsets().inputPlayer) || !*(void **)(input+GenshinOffsets().inputManager)) {
        Log(@"desktop_control input_not_ready");return false;
    }
    void *method=lookup(*(void **)owner,ControlSwitchMethodName(),2,0);
    if (!method || ManagedMethodCode(method)!=(uintptr_t)EngineFunction<void *>(0x1068c8ab0)) {
        Log(@"desktop_control refused_unexpected_switch_method");return false;
    }
    // Login constructs input-hint widgets before controller action maps exist.
    // Bootstrap keyboard/mouse, then restore a saved controller through the
    // complete native hot-swap only after the real main-scene HUD is present.
    int mode=2;
    bool partial=false;void *arguments[]={&mode,&partial},*exception=nullptr;
    invoke(method,owner,arguments,&exception);
    Log(@"desktop_control after=%d exception=%p",*(int *)(owner+GenshinOffsets().ownerInputMode),exception);
    if (exception) Log(@"desktop_control exception_class=%s",className(*(void **)exception));
    bool restored=!exception&&*(int *)(owner+GenshinOffsets().ownerInputMode)==mode;
    if(restored) {
        DesktopControlPreference::bootstrapReady=true;
        bool deferred=DesktopControlPreference::LaunchMode()==3;
        DesktopControlPreference::startupRestored=!deferred;
        Log(@"control_preference bootstrap_ready=1 saved=%d actual=%d controller_count=%d controller_restore_deferred=%d stored_preference_unchanged=1",DesktopControlPreference::SavedMode(),mode,joystickCount,deferred);
    }
    return restored;
}

static void InspectActionEnum(void) {
    auto findClass=EngineFunction<void *(*)(const char *,const char *,const char *)>(0x10f057bd4);
    auto nextField=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto fieldName=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
    auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
    auto staticValue=EngineFunction<void (*)(void *,void *)>(0x10ea03c4c);
    void *cls=findClass("Assembly-CSharp.dll","","DAIDPONIGDD"),*it=nullptr;
    if (!cls) return;
    while (void *field=nextField(cls,&it)) {
        if (!(flags(field)&0x40)) continue;
        uint64_t value=0;staticValue(field,&value);
        Log(@"input_action name=%s id=%llu",fieldName(field),value);
    }
}

static void *ReadManagedProperty(void *object,const char *name,bool scalar=false) {
    if (!object) return nullptr;
    auto lookup=EngineFunction<void *(*)(void *,const char *,int,int)>(0x10f0572e8);
    auto invoke=EngineFunction<void *(*)(void *,void *,void **,void **)>(0x10e9bb768);
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    void *method=lookup(*(void **)object,name,0,0),*exception=nullptr;
    if (!method) {Log(@"rewired_property missing=%s",name);return nullptr;}
    void *result=invoke(method,object,nullptr,&exception);
    if (!exception && result) {
        if (scalar) Log(@"rewired_property name=%s type=%s value=%u",name,className(*(void **)result),*(uint32_t *)((char *)result+16));
        else Log(@"rewired_property name=%s type=%s object=%p",name,className(*(void **)result),result);
    } else Log(@"rewired_property name=%s result=%p exception=%p",name,result,exception);
    return exception?nullptr:result;
}

static void EnableInputDevice(void *device) {
    if (!device) return;
    auto lookup=EngineFunction<void *(*)(void *,const char *,int,int)>(0x10f0572e8);
    auto invoke=EngineFunction<void *(*)(void *,void *,void **,void **)>(0x10e9bb768);
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    void *method=lookup(*(void **)device,"set_enabled",1,0),*exception=nullptr;
    if (!method) {Log(@"device_enable missing_setter type=%s",className(*(void **)device));return;}
    bool enabled=true;void *arguments[]={&enabled};
    invoke(method,device,arguments,&exception);
    Log(@"device_enable type=%s exception=%p foreground=%d",className(*(void **)device),exception,
        UIApplication.sharedApplication.applicationState==UIApplicationStateActive);
    ReadManagedProperty(device,"get_enabled",true);
}

static float (*gameAxisReader)(void *,int,void *);
static void *gameAxisMethod;
static void *rewiredKeyboard,*rewiredKeyMethod;
static bool (*rewiredKeyReader)(void *,int,void *);
static void InspectInputObject(void *object,const char *label) {
    if (!object) return;
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto fieldName=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
    auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
    auto offset=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    for (void *cls=*(void **)object;cls;cls=parent(cls)) {
        void *it=nullptr;
        while (auto field=(uintptr_t *)next(cls,&it)) {
            unsigned o=offset(field);if ((flags(field)&0x10) || o<16 || o>4096) continue;
            void *fc=fromType(ManagedFieldType(field));if (!fc) continue;
            const char *n=className(fc);
            if (!strcmp(n,"Boolean")) Log(@"input_object label=%s field=%s type=%s offset=%u value=%u",label,fieldName(field),n,o,*(uint8_t *)((char *)object+o));
            else if (!strcmp(n,"Int32")) Log(@"input_object label=%s field=%s type=%s offset=%u value=%d",label,fieldName(field),n,o,*(int *)((char *)object+o));
            else Log(@"input_object label=%s field=%s type=%s offset=%u",label,fieldName(field),n,o);
        }
    }
}
static void InspectRewiredState(void) {
    if (!IsCompatibleRuntime()) return;
    char *owner=VerifiedInputOwner();if (!owner) return;
    char *input=*(char **)(owner+GenshinOffsets().ownerGameInput);if (!input) return;
    void *player=*(void **)(input+GenshinOffsets().inputPlayer);
    void *controllers=player?*(void **)((char *)player+GenshinOffsets().playerControllers):nullptr;
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    if (!controllers || strcmp(className(*(void **)controllers),"ControllerHelper")) return;
    ReadManagedProperty(controllers,"get_hasKeyboard",true);
    ReadManagedProperty(controllers,"get_hasMouse",true);
    rewiredKeyboard=ReadManagedProperty(controllers,"get_Keyboard");
    void *mouse=ReadManagedProperty(controllers,"get_Mouse");
    ReadManagedProperty(rewiredKeyboard,"get_enabled",true);
    ReadManagedProperty(mouse,"get_enabled",true);
    EnableInputDevice(rewiredKeyboard);
    EnableInputDevice(mouse);
    ReadManagedProperty(*(void **)(input+GenshinOffsets().inputManager),"get_enabled",true);
    if(diagnosticsEnabled) {InspectInputObject(player,"Player");InspectInputObject(rewiredKeyboard,"Keyboard");InspectInputObject(mouse,"Mouse");}
    void *maps=UsesLegacyMetadataLayout()?*(void **)((char *)controllers+16):nullptr;
    if (maps && diagnosticsEnabled) {
        InspectInputObject(maps,"Maps");
        InspectMethods(*(void **)maps,"Rewired.Player",className(*(void **)maps));
    }
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto methodName=EngineFunction<const char *(*)(void *)>(0x10e9bb6f0);
    auto paramCount=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    auto paramType=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    void *it=nullptr;
    if (rewiredKeyboard) {
        InspectMethods(*(void **)rewiredKeyboard,"Rewired","Keyboard");
        while (void *method=next(*(void **)rewiredKeyboard,&it)) {
            if (strcmp(methodName(method),"GetKey") || paramCount(method)!=1) continue;
            void *pc=fromType(paramType(method,0));
            if (!pc || (strcmp(className(pc),"KeyCode") && strcmp(className(pc),"Int32"))) continue;
            rewiredKeyMethod=method;rewiredKeyReader=(bool (*)(void *,int,void *))ManagedMethodCode(method);
            Log(@"rewired_key_reader installed type=%s code=%p",className(pc),rewiredKeyReader);
        }
    }
    it=nullptr;
    while (void *method=next(*(void **)player,&it)) {
        if (strcmp(methodName(method),"GetAxis") || paramCount(method)!=1) continue;
        void *pc=fromType(paramType(method,0));
        if (!pc || strcmp(className(pc),"Int32")) continue;
        gameAxisMethod=method;gameAxisReader=(float (*)(void *,int,void *))ManagedMethodCode(method);
        Log(@"game_action_reader installed code=%p",gameAxisReader);
    }
}

static void InspectControlModeOwners(void) {
    if(!UsesLegacyMetadataLayout()) return; // This scanner belongs to the legacy metadata ABI.
    if (!IsCompatibleRuntime()) return;
    auto findClass=EngineFunction<void *(*)(const char *,const char *,const char *)>(0x10f057bd4);
    auto nextField=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
    auto fieldName=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
    auto fieldFlags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
    auto fieldOffset=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);
    auto className=EngineFunction<const char *(*)(void *)>(0x10e9bb600);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    auto staticValue=EngineFunction<void (*)(void *,void *)>(0x10ea03c4c);
    for (const char *name : {"EKAEMFGFEMJ","EAKFNBEONOH"}) {
        void *cls=findClass("Assembly-CSharp.dll","",name);if (!cls) continue;
        void *it=nullptr;
        while (void *field=nextField(cls,&it)) {
            if (!(fieldFlags(field)&0x40)) continue;
            uint64_t value=0;staticValue(field,&value);
            Log(@"control_enum type=%s member=%s value=%llu",name,fieldName(field),value);
        }
    }
    auto manager=EngineFunction<char *(*)()>(0x10f05b6fc)();
    auto images=*(char ***)(manager+0x208);char *img=images[60];
    uint32_t count=*(uint32_t *)(img+0x30)+0x9bd93914u;
    uint32_t start=*(uint32_t *)(img+0x48)^0x79f2e891u;
    if (count>300000) return;
    auto typeDef=EngineFunction<char *(*)(uint32_t)>(0x10ea0e980);
    auto typeClass=EngineFunction<void *(*)(uint32_t)>(0x10ea0c3b4);
    for (uint32_t j=0;j<count;++j) {
        char *def=typeDef(start+j);if (!def) continue;
        unsigned fields=(uint16_t)(*(uint16_t *)(def+0x30)-0x790eu);
        if (!fields || fields>3000) continue;
        void *cls=typeClass(start+j),*it=nullptr;if (!cls) continue;
        bool ownsInput=false;
        while (auto field=(uintptr_t *)nextField(cls,&it)) {
            void *fc=fromType(ManagedFieldType(field));if (!fc) continue;
            const char *n=className(fc);
            if (strcmp(n,"EKAEMFGFEMJ") && strcmp(n,"ELKFCMHKCNI") && strcmp(n,"KHDBFGPPFBM")) continue;
            unsigned flags=fieldFlags(field);uint64_t value=0;
            if ((flags&0x10) && !(flags&0x40)) staticValue(field,&value);
            Log(@"control_owner index=%u class=%s field=%s type=%s offset=%u flags=%x static_value=%llx",
                start+j,className(cls),fieldName(field),n,fieldOffset(field),flags,value);
            if (strcmp(n,"EKAEMFGFEMJ")) ownsInput=true;
        }
        if (ownsInput) InspectMethods(cls,"",className(cls));
    }
    Log(@"control_owner_scan finished");
}

static char *nativeInputManager;
static void (*nativeKeyConsumer)(int,bool);
static int UnityKeyForHID(int key) {
    if (key>=4 && key<=29) return 'a'+key-4;
    if (key>=30 && key<=38) return '1'+key-30;
    if (key==39) return '0';
    if (key>=58 && key<=69) return 282+key-58;
    if (key>=104 && key<=106) return 294+key-104; // Unity legacy enum ends at F15.
    switch (key) {
        case 40:return 13;case 41:return 27;case 42:return 8;case 43:return 9;case 44:return 32;
        case 45:return '-';case 46:return '=';case 47:return '[';case 48:return ']';case 49:return '\\';
        case 51:return ';';case 52:return '\'';case 53:return '`';case 54:return ',';case 55:return '.';case 56:return '/';
        case 79:return 275;case 80:return 276;case 81:return 274;case 82:return 273;
        case 224:return 306;case 225:return 304;case 226:return 308;case 227:return 310;
        case 228:return 305;case 229:return 303;case 230:return 307;case 231:return 309;
        default:return 0;
    }
}
static void ForwardNativeKey(int hid,bool pressed) {
    if (!nativeInputManager) return;
    int key=UnityKeyForHID(hid);
    if (!key) return;
    if (nativeKeyConsumer) { nativeKeyConsumer(key,pressed);return; }
    auto setter=EngineFunction<void (*)(void *,int,bool)>(0x10eec31b8);
    setter(nativeInputManager,key,pressed);
    auto getter=EngineFunction<bool (*)(int)>(0x10e864028);
    if (keyboardEvents<=8) Log(@"keyboard_forwarded pressed=%d engine_readback=%d",pressed,getter(key));
}

static void InspectManagedInput(void) {
    if (!IsCompatibleRuntime()) { Log(@"managed_probe refused_unknown_build"); return; }
    Log(@"managed_probe begin");
    InspectManagedClass("UnityEngine.InputModule.dll","UnityEngine","Input");
    InspectManagedClass("UnityEngine.CoreModule.dll","UnityEngine","Input");
    InspectManagedClass("UnityEngine.CoreModule.dll","UnityEngine","Application");
    InspectManagedClass("Rewired_Core.dll","Rewired","ReInput");
    InspectManagedClass("Assembly-CSharp.dll","MoleMole","InputManager");
    InspectManagedClass("Assembly-CSharp.dll","MoleMole","Keyboard");
    InspectManagedClass("Assembly-CSharp.dll","MoleMole","UIUtils");
    InspectManagedClass("Assembly-CSharp-firstpass.dll","MoleMole","GlobalVars");
    InspectManagedClass("MCPForUnity.Runtime.dll","MCPForUnity.Runtime.Game","GameCharacterControl");
    InspectGameInputTypes();
    InspectInputFields();
    Log(@"managed_probe end");
}

static void NativeInputSelfTest(void) {
    if (!IsCompatibleRuntime()) return;
    auto manager=EngineFunction<char *(*)()>(0x10eec0180)();
    Log(@"native_input_manager pointer=%p",manager);
    if (!manager) return;
    auto held=*(uint32_t **)(manager+0x68);
    auto down=*(uint32_t **)(manager+0x88);
    auto up=*(uint32_t **)(manager+0xa8);
    if (!held || !down || !up || *(uint64_t *)(manager+0x78)<11) {
        Log(@"native_input_test unavailable_buffers"); return;
    }
    nativeInputManager=manager;
    auto getKey=EngineFunction<bool (*)(int)>(0x10e864028);
    auto getDown=EngineFunction<bool (*)(int)>(0x10e864030);
    auto getUp=EngineFunction<bool (*)(int)>(0x10e86402c);
    // Test synchronously, then restore before Unity's next frame can observe it.
    for (int key : {119,323}) {
        unsigned word=key/32,bit=1u<<(key%32);
        uint32_t oldHeld=held[word],oldDown=down[word],oldUp=up[word];
        held[word]&=~bit;down[word]&=~bit;up[word]&=~bit;
        bool initial=getKey(key);
        held[word]|=bit;down[word]|=bit;
        bool pressed=getKey(key),justDown=getDown(key);
        held[word]&=~bit;down[word]&=~bit;up[word]|=bit;
        bool released=!getKey(key),justUp=getUp(key);
        held[word]=oldHeld;down[word]=oldDown;up[word]=oldUp;
        Log(@"native_input_test device=%s initial=%d pressed=%d down=%d released=%d up=%d restored=1",
            key==119?"keyboard":"mouse_button",initial,pressed,justDown,released,justUp);
    }
    auto stringNew=EngineFunction<void *(*)(const char *)>(0x10e9bb794);
    auto getAxis=EngineFunction<float (*)(void *)>(0x10e864058);
    char *axes=*(char **)(manager+0x40);
    uint64_t count=*(uint64_t *)(manager+0x50);
    Log(@"native_input_axes count=%llu",count);
    if (count>2000) return;
    for (uint64_t i=0;i<count;++i) {
        char *axis=axes+i*0xb8;
        const char *name=*(const char **)axis;
        if (!name) name=axis+8;
        if (!strcmp(name,"Mouse X") || !strcmp(name,"Mouse Y") || !strcmp(name,"Mouse ScrollWheel")) {
            float *value=(float *)(axis+0x98);float saved=*value;
            void *text=stringNew(name);
            *value=0.375f;
            float readback=getAxis(text);
            *value=saved;
            Log(@"native_axis_test name=%s readback=%f restored=1",name,readback);
        }
    }
}
