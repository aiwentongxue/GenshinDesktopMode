#include "mouse_drag_state.h"
#include "drag_geometry.h"

namespace NativeCameraPointer {
using namespace SceneUIProbe;
struct Vector2 {float x,y;};
struct Vector3 {float x,y,z;};
static void (*originalUpdate)(void *,void *),(*originalDisable)(void *,void *);
static void (*swipe)(void *,Vector2,void *),(*speed)(void *,Vector3,void *);
static void *swipeMethod,*speedMethod,*gotoMethod,*dialogsMethod;
static bool installed;
static void *trackedTouch,*trackedCamera;
static DesktopMouseDragState drag;
static DesktopDragGeometry capturedGeometry;
static bool mapActive;

static void *Method(void *cls,const char *name,const char *result,
                    std::initializer_list<const char *> parameters) {
    void *method=lookup(cls,name,(int)parameters.size(),0);
    if(!method || !ManagedMethodCode(method)) return nullptr;
    auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    auto returns=EngineFunction<void *(*)(void *)>(0x10e9bb6e8);
    auto parameter=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    if(strcmp(className(from(returns(method))),result)) return nullptr;
    unsigned index=0;
    for(const char *expected:parameters)
        if(strcmp(className(from(parameter(method,index++))),expected)) return nullptr;
    return method;
}
static bool RegisterOnlyPrologue(const void *code) {
    const uint32_t *p=(const uint32_t *)code;
    for(unsigned i=0;i<4;++i) {
        uint32_t op=p[i];
        bool storePair=(op&0x3b400000u)==0x29000000u && ((op>>5)&31)==31;
        bool stackSubtract=(op&0xffc003ffu)==0xd10003ffu;
        bool framePointer=(op&0xffc003ffu)==0x910003fdu;
        bool moveRegister=(op&0xffe0ffe0u)==0xaa0003e0u;
        if(!storePair&&!stackSubtract&&!framePointer&&!moveRegister) return false;
    }
    return true;
}
static void *Trampoline(void *target) {
    if(!RegisterOnlyPrologue(target)) return nullptr;
    void *code=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(code==MAP_FAILED) return nullptr;
    memcpy(code,target,16);
    NativeBridge::AbsoluteJump((char *)code+16,(uintptr_t)target+16);
    sys_icache_invalidate(code,32);
    if(mprotect(code,4096,PROT_READ|PROT_EXEC)) {munmap(code,4096);return nullptr;}
    return code;
}
static void Goto(void *camera,int mode) {
    void *args[]={&mode,nullptr};Call(gotoMethod,camera,args);
}
static void Action(void *touch,size_t offset) {
    void *action=*(void **)((char *)touch+offset);
    if(action) Call(lookup(*(void **)action,"Invoke",0,0),action);
}
static void End(void *touch,void *camera) {
    // Mouse release has no touch momentum. Keep the game's own boundary
    // recovery state, with zero residual velocity and no delayed release.
    speed(camera,Vector3{},speedMethod);Goto(camera,2);
    Action(touch,80);
    NativeBridge::mapDragActive=false;
    Log(@"native_camera_mouse drag=0 source=mouse_absolute_pixels touch_objects=0");
}
static bool HasWidget(void *pointer) {
    void *hit=Property(pointer,"get_pointerEnter");
    static void *componentType=TeleportCursorBridge::TypeObject(
        "UnityEngine.CoreModule.dll","UnityEngine","Component");
    for(unsigned depth=0;hit&&depth<16;++depth) {
        void *method=TeleportCursorBridge::OneTypeArgument(*(void **)hit,"GetComponents");
        void *args[]={componentType},*array=Call(method,hit,args);
        uint64_t count=array?*(uint64_t *)((char *)array+24):0;
        if(count>100) return true;
        for(uint64_t i=0;i<count;++i) {
            void *component=((void **)((char *)array+32))[i];if(!component) continue;
            void *cls=*(void **)component;
            auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
            for(unsigned level=0;cls&&level<8;cls=parent(cls),++level) {
                const char *name=className(cls);
                if(!strcmp(name,"Selectable")||!strcmp(name,"ScrollRect")||
                   !strcmp(name,"MonoMapMarkTips")||!strcmp(name,"MonoMapMarkTipsPanel")) return true;
            }
        }
        void *transform=Property(hit,"get_transform");
        transform=Property(transform,"get_parent");hit=Property(transform,"get_gameObject");
    }
    return false;
}
static bool MayBegin(void *touch) {
    // Resolve the native UI raycast only on a new physical press. Buttons,
    // sliders and lists keep their own PointerEventData drag handlers.
    void *blocked=Call(dialogsMethod,touch);
    if(blocked&&*(bool *)((char *)blocked+16)) return false;
    char *owner=NativePC::InputOwner();
    void *module=owner?*(void **)(owner+GenshinOffsets().ownerModule):nullptr;
    void *pointer=module?NativePC::MousePointer(module):nullptr;
    return !pointer||!HasWidget(pointer);
}
static int MapContext(void) {return installed?(mapActive?1:0):-1;}
static void Update(void *object,void *method) {
    originalUpdate(object,method);
    char *touch=(char *)object;
    char *context=*(char **)(touch+40);
    static void *lastContextClass;static size_t monoOffset;
    void *contextClass=context?*(void **)context:nullptr;
    if(contextClass!=lastContextClass) {
        lastContextClass=contextClass;monoOffset=0;
        // The map context is identified by its typed view field, never by
        // a game version, region, translated title or obfuscated class name.
        if(contextClass) {
            auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);
            auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);
            auto offset=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);
            auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
            void *it=nullptr;
            while(void *field=next(contextClass,&it))
                if(!(flags(field)&0x10)&&!strcmp(className(from(ManagedFieldType(field))),"MonoInLevelMapPage"))
                    monoOffset=offset(field);
        }
    }
    if(!monoOffset) {
        if(object==trackedTouch) {
            if(drag.cancel().ended&&trackedCamera) End(object,trackedCamera);
            trackedTouch=trackedCamera=nullptr;mapActive=false;
            NativeBridge::mapDragActive=false;
        }
        return;
    }
    void *mono=*(void **)(context+monoOffset);
    // This active Unity callback owns the touch proxy and camera lifetime.
    // No stale scene-wide component pointers are retained across callbacks.
    void *camera=*(void **)(touch+56);
    static void *monoClass,*activeMethod;
    static bool (*isActive)(void *,void *);
    if(mono&&*(void **)mono!=monoClass) {
        monoClass=*(void **)mono;
        activeMethod=Method(monoClass,"get_isActiveAndEnabled","Boolean",{});
        isActive=activeMethod?(decltype(isActive))ManagedMethodCode(activeMethod):nullptr;
    }
    bool active=mono&&camera&&isActive&&NativeBridge::nativeDesktopMouseUI.load()&&
        !NativeBridge::nativeOverlayActive&&isActive(mono,activeMethod);
    trackedTouch=object;trackedCamera=camera;mapActive=active;
    static void *described;
    if(active&&camera!=described) {
        described=camera;
        Log(@"native_camera_mouse map_bound=1 typed_view_offset=%zu camera=%p",monoOffset,camera);
    }
    if(!camera||!NativeBridge::screenWidth||!NativeBridge::screenHeight) return;
    bool held=(NativeBridge::physicalMouseSnapshot.load()&1)!=0;
    bool foreground=!pointerCaptureSuspended&&NativeBridge::IsForeground();
    float x=*(float *)(nativeInputManager+0xd0),y=*(float *)(nativeInputManager+0xd4);
    int w=NativeBridge::screenWidth(NativeBridge::screenWidthMethod);
    int h=NativeBridge::screenHeight(NativeBridge::screenHeightMethod);
    CGRect bounds{};
    bool inside=x>=0&&y>=0&&x<w&&y<h&&GameContentBounds(bounds);
    bool available=active&&foreground&&WantsVisibleCursor()&&inside;
    bool fresh=held&&!drag.held;
    if(fresh&&available)available=capturedGeometry.capture(w,h,bounds.size.width,bounds.size.height);
    else if(drag.armed)available=available&&capturedGeometry.matches(w,h,bounds.size.width,bounds.size.height);
    auto step=drag.sample(held,available,fresh&&available&&MayBegin(object),
        NativeBridge::mapFocusRevision.load(),x,y);
    if(step.ended) End(object,camera);
    if(step.began) {
        speed(camera,Vector3{},speedMethod);Goto(camera,1);Action(object,72);
        NativeBridge::mapDragActive=true;
        Log(@"native_camera_mouse drag=1 source=mouse_absolute_pixels touch_objects=0");
    }
    if(step.moved) {
        swipe(camera,Vector2{step.x,step.y},swipeMethod);
        static uint64_t pans=0;
        if(++pans<=3||pans%240==0)
            Log(@"native_camera_mouse_pan frame=%llu delta=(%.2f,%.2f) virtual_touch=0",pans,step.x,step.y);
    }
}
static void Disable(void *object,void *method) {
    if(object==trackedTouch) {
        if(drag.cancel().ended&&trackedCamera) End(object,trackedCamera);
        trackedTouch=trackedCamera=nullptr;mapActive=false;
        NativeBridge::mapDragActive=false;
    }
    originalDisable(object,method);
}
static bool TryInstall(void) {
    if(installed) return true;
    if(!NativeBridge::enabled||!NativeBridge::frames.load()) return false;
    void *cls=findClass("Assembly-CSharp.dll","MoleMole","MonoExUITouch");
    void *cam=findClass("Assembly-CSharp.dll","MoleMole","MonoExUICamera");
    if(!cls||!cam) return false;
    // Validate every field used by this optional adapter against runtime
    // metadata. An unrecognized layout leaves the accepted R37 path intact.
    if(!HasManagedField(cls,nullptr,40,"GGOFADHAPNB")||
       !HasManagedField(cls,nullptr,56,"MonoExUICamera")||
       !HasManagedField(cls,nullptr,72,"Action")||!HasManagedField(cls,nullptr,80,"Action")) return false;
    void *update=Method(cls,"Update","Void",{}),*disable=Method(cls,"OnDisable","Void",{});
    dialogsMethod=Method(cls,"HasDialogsAtTop","Boolean",{});
    swipeMethod=Method(cam,"SwipeCamera","Void",{"Vector2"});
    speedMethod=Method(cam,"SetCameraSpeed","Void",{"Vector3"});
    gotoMethod=Method(cam,"GotoState","Void",{"HLLEANAOOGC","Object"});
    if(!update||!disable||!dialogsMethod||!swipeMethod||!speedMethod||!gotoMethod) return false;
    void *targets[]={(void *)ManagedMethodCode(update),(void *)ManagedMethodCode(disable)};
    uint8_t saved[2][16];
    for(unsigned i=0;i<2;++i) {
        if(!RegisterOnlyPrologue(targets[i])) return false;
        memcpy(saved[i],targets[i],16);
    }
    originalUpdate=(void (*)(void *,void *))Trampoline(targets[0]);
    originalDisable=(void (*)(void *,void *))Trampoline(targets[1]);
    if(!originalUpdate||!originalDisable) return false;
    void *replacements[]={(void *)Update,(void *)Disable};
    for(unsigned i=0;i<2;++i) {
        uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)replacements[i]);
        if(!NativeBridge::PatchCode(targets[i],jump,16)) {
            for(unsigned j=0;j<=i;++j) NativeBridge::PatchCode(targets[j],saved[j],16);
            return false;
        }
    }
    swipe=(decltype(swipe))ManagedMethodCode(swipeMethod);
    speed=(decltype(speed))ManagedMethodCode(speedMethod);
    installed=true;NativeBridge::nativePointerGestures=true;
    NativeBridge::mapContextReader=MapContext;
    Log(@"native_camera_mouse installed=1 map=1 generic_ui=pointer_event_data virtual_touch=0");
    return true;
}
}
