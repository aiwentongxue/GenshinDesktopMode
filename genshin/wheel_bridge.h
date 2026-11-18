// Map the existing desktop cursor into the dial's own RectTransform. The mobile
// client otherwise accumulates a second, separately scaled relative position.
namespace WheelBridge {
using namespace SceneUIProbe;
struct Vector2 {float x,y;};
struct Rect {float x,y,width,height;};
static void (*originalInput)(void *,Vector2,void *);
static void *screenToLocal,*canvasType,*componentByType;

static void *FindTypeOverload(void *cls,const char *name) {
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto methodName=EngineFunction<const char *(*)(void *)>(0x10e9bb6f0);
    auto count=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    auto parameter=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto fromType=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    void *it=nullptr;
    while(void *m=next(cls,&it)) {
        if(strcmp(methodName(m),name) || count(m)!=1) continue;
        void *pc=fromType(parameter(m,0));
        if(pc && !strcmp(className(pc),"Type")) return m;
    }
    return nullptr;
}
static bool Prepare(void) {
    if(screenToLocal && canvasType && componentByType) return true;
    void *utility=nullptr,*canvas=nullptr;
    for(const char *assembly:{"UnityEngine.UIModule.dll","UnityEngine.CoreModule.dll","UnityEngine.dll"}) {
        if(!utility) utility=findClass(assembly,"UnityEngine","RectTransformUtility");
        if(!canvas) canvas=findClass(assembly,"UnityEngine","Canvas");
    }
    void *component=findClass("UnityEngine.CoreModule.dll","UnityEngine","Component");
    if(!utility || !canvas || !component) return false;
    screenToLocal=lookup(utility,"ScreenPointToLocalPointInRectangle",4,0);
    void *type=EngineFunction<void *(*)(void *)>(0x10e9bb654)(canvas);
    canvasType=EngineFunction<void *(*)(void *)>(0x10e9bb7b8)(type);
    componentByType=FindTypeOverload(component,"GetComponent");
    return screenToLocal && canvasType && componentByType;
}
static bool PositionInDial(void *mono,Vector2 &position) {
    if(!Prepare() || !NativeBridge::screenWidth || !NativeBridge::screenHeight) return false;
    void *cursor=*(void **)((char *)mono+GenshinOffsets().monoCursor);if(!cursor) return false;
    void *parent=Property(cursor,"get_parent");if(!parent) return false;
    void *rectBox=Property(parent,"get_rect");if(!rectBox) return false;
    Rect rect=*(Rect *)((char *)rectBox+16);
    if(!(rect.width>1 && rect.height>1)) return false;
    static void *previousParent,*camera;
    if(previousParent!=parent) {
        previousParent=parent;camera=nullptr;
        void *transform=parent;
        for(unsigned i=0;i<20 && transform;++i) {
            void *args[]={canvasType};
            void *canvas=Call(componentByType,transform,args);
            if(canvas) {camera=Property(canvas,"get_worldCamera");break;}
            transform=Property(transform,"get_parent");
        }
        Log(@"wheel_geometry parent=%@ rect=(%.1f,%.1f,%.1f,%.1f) camera=%d",Name(parent),
            rect.x,rect.y,rect.width,rect.height,camera!=nullptr);
    }
    Vector2 screen,local{};
    {
        std::lock_guard<std::mutex> guard(NativeBridge::mutex);
        if(!NativeBridge::hasPointerPosition) return false;
        screen={NativeBridge::normalizedX*NativeBridge::ScreenWidthValue(),
            NativeBridge::normalizedY*NativeBridge::ScreenHeightValue()};
    }
    void *args[]={parent,&screen,camera,&local};
    void *result=Call(screenToLocal,nullptr,args);
    if(!result || !*(bool *)((char *)result+16)) return false;
    position={(local.x-rect.x)/rect.width*2-1,(local.y-rect.y)/rect.height*2-1};
    return std::isfinite(position.x) && std::isfinite(position.y);
}
static void Input(void *object,Vector2 delta,void *method) {
    // Obfuscated context names differ between clients. Check the actual fields
    // once, before applying the verified native-coordinate adaptation.
    if(!UsesLegacyMetadataLayout()) {
        static void *checkedClass;static bool schemaValid;
        void *cls=*(void **)object;
        if(cls!=checkedClass) {
            const auto &o=GenshinOffsets();checkedClass=cls;
            schemaValid=HasManagedField(cls,nullptr,o.wheelInputMode,InputModeClassName()) &&
                HasManagedField(cls,nullptr,o.wheelMono,"MonoInLevelVirtualDialDialog") &&
                HasManagedField(cls,nullptr,o.wheelPosition,"Vector2");
            Log(@"wheel_managed_layout verified=%d class=%s",schemaValid,className(cls));
        }
        if(!schemaValid) {originalInput(object,delta,method);return;}
    }
    bool mapped=false;Vector2 desired{};
    if(*(int *)((char *)object+GenshinOffsets().wheelInputMode)==2 && NativeBridge::IsForeground()) {
        void *mono=*(void **)((char *)object+GenshinOffsets().wheelMono);
        if(mono && !strcmp(className(*(void **)mono),"MonoInLevelVirtualDialDialog") && PositionInDial(mono,desired)) {
            Vector2 previous=*(Vector2 *)((char *)object+GenshinOffsets().wheelPosition);
            delta={desired.x-previous.x,desired.y-previous.y};mapped=true;
        }
    }
    originalInput(object,delta,method);
    static uint64_t frames=0;
    if(mapped && (++frames<=5 || frames%60==0))
        Log(@"wheel_cursor_mapping normalized=(%.3f,%.3f) selected=%d",desired.x,desired.y,*(int *)((char *)object+GenshinOffsets().wheelSelected));
}
static void Install(void) {
    if(!IsCompatibleRuntime()) return;
    void *target=EngineFunction<void *>(0x10218d388);
    const uint8_t expected[]={0xff,0x03,0x01,0xd1,0xe9,0x23,0x01,0x6d,
        0xf4,0x4f,0x02,0xa9,0xfd,0x7b,0x03,0xa9};
    if(!RuntimePrologueMatches(0x10218d388,target)) {Log(@"wheel_bridge refused_unexpected_code");return;}
    void *trampoline=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(trampoline==MAP_FAILED) return;
    memcpy(trampoline,target,16);NativeBridge::AbsoluteJump((char *)trampoline+16,(uintptr_t)target+16);
    sys_icache_invalidate(trampoline,32);
    if(mprotect(trampoline,4096,PROT_READ|PROT_EXEC)) return;
    originalInput=(void (*)(void *,Vector2,void *))trampoline;
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)Input);
    Log(@"wheel_bridge installed=%d absolute_cursor_geometry=1",NativeBridge::PatchCode(target,jump,16));
}
}
