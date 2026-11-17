#pragma once
#include "codex_click_profiles.h"
#include "codex_drag_profiles.h"
#include "codex_pointer_state.h"
#include "character_mouse_units.h"
namespace NativeCodexPointer {
using namespace SceneUIProbe;
struct V2 {float x,y;};
static constexpr float dragThresholdPoints=5;
// Carousel-only gain; cursor speed and character/gameplay camera are unchanged.
static constexpr float carouselGain=.05f;
static CodexPointerState gesture;
static int sceneIdentity,renderWidth,renderHeight;
static double logicalWidth,logicalHeight;
static float mouseSensitivityX=.1f;
static void *targetScene,*capturedPage;
static void *pageType,*findPage,*clickMethod,*dragMethod,*deltaMethod,*activeMethod;
static void (*click)(void *,V2,void *);
static void (*setDragging)(void *,bool,V2,float,void *);
static void (*rotateCodex)(void *,float,void *);
static bool (*pageActive)(void *,void *);
static int Identity(void *object){
    static void *method;
    static int (*getID)(void *,void *);
    if(!method){
        method=NativeCameraPointer::Method(*(void **)object,"GetInstanceID","Int32",{});
        getID=method?(decltype(getID))ManagedMethodCode(method):nullptr;
    }
    return getID?getID(object,method):0;
}

static void *VisiblePage(){
    if(!pageType)pageType=TeleportCursorBridge::TypeObject("Assembly-CSharp.dll","MoleMole","MonoCodexPage");
    if(!findPage)findPage=TeleportCursorBridge::OneTypeArgument(findClass("UnityEngine.CoreModule.dll","UnityEngine","Object"),"FindObjectsOfType");
    void *args[]={pageType},*array=pageType&&findPage?Call(findPage,nullptr,args):nullptr;
    uint64_t n=array?*(uint64_t *)((char *)array+24):0;if(n>16)return nullptr;
    for(uint64_t i=0;i<n;++i){void *page=((void **)((char *)array+32))[i];
        if(!page)continue;
        if(!pageActive){
            activeMethod=NativeCameraPointer::Method(*(void **)page,"get_isActiveAndEnabled","Boolean",{});
            pageActive=activeMethod?(decltype(pageActive))ManagedMethodCode(activeMethod):nullptr;
        }
        if(pageActive&&pageActive(page,activeMethod)&&Bool(Property(page,"get_gameObject"),"get_activeInHierarchy"))return page;
    }
    return nullptr;
}
static void *TypedBody(void *scene,uintptr_t address,const uint8_t *expected,
                        std::initializer_list<const char *> parameters){
    const void *body=RuntimeImageRange(address,96,false);if(!body||memcmp(body,expected,96))return nullptr;
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto count=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    auto param=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto ret=EngineFunction<void *(*)(void *)>(0x10e9bb6e8);
    auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    void *it=nullptr;
    while(void *m=next(*(void **)scene,&it)){
        if(ManagedMethodCode(m)!=(uintptr_t)body||count(m)!=parameters.size())continue;
        if(strcmp(className(from(ret(m))),"Void"))return nullptr;
        unsigned index=0;
        for(const char *type:parameters)
            if(strcmp(className(from(param(m,index++))),type))return nullptr;
        return m;
    }
    return nullptr;
}
static bool Bind(void *scene){
    if(click)return true;if(!IsModernRuntime()||!scene||strcmp(className(*(void **)scene),"MonoSceneUISetup"))return false;
    bool global=IsGlobalModernRuntime();
    clickMethod=TypedBody(scene,global?CodexClickGlobalAddress:CodexClickCNAddress,
        global?CodexClickGlobalExpected:CodexClickCNExpected,{"Vector2"});
    if(!clickMethod)return false;
    click=(decltype(click))ManagedMethodCode(clickMethod);
    dragMethod=TypedBody(scene,global?CodexDragGlobalAddress:CodexDragCNAddress,
        global?CodexDragGlobalExpected:CodexDragCNExpected,{"Boolean","Vector2","Single"});
    deltaMethod=TypedBody(scene,global?CodexDeltaGlobalAddress:CodexDeltaCNAddress,
        global?CodexDeltaGlobalExpected:CodexDeltaCNExpected,{"Single"});
    if(dragMethod&&deltaMethod){
        setDragging=(decltype(setDragging))ManagedMethodCode(dragMethod);
        rotateCodex=(decltype(rotateCodex))ManagedMethodCode(deltaMethod);
        NSString *settings=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Data/globalgamemanagers"];
        NSData *asset=[NSData dataWithContentsOfFile:settings options:NSDataReadingMappedIfSafe error:nil];
        bool parsed=asset&&CharacterMouseUnits::sensitivity((const uint8_t *)asset.bytes,asset.length,"Mouse X",0,mouseSensitivityX);
        Log(@"codex_mouse drag_bound=1 native_carousel=1 native_snap=1 mouse_axis=%.3f carousel_gain=%.2f parsed=%d virtual_touch=0 profile=%s",mouseSensitivityX,carouselGain,parsed,RuntimeProfileName());
    }else Log(@"codex_mouse drag_bound=0 refused_signature=1 profile=%s",RuntimeProfileName());
    Log(@"codex_mouse bound=1 native_3d_raycast=1 virtual_touch=0 profile=%s",RuntimeProfileName());
    return true;
}
static bool Surface(){
    void *pointer=NativePC::configuredModule?NativePC::MousePointer(NativePC::configuredModule):nullptr;
    return pointer&&!Property(pointer,"get_pointerEnter")&&!Property(pointer,"get_pointerPress")&&!Property(pointer,"get_pointerDrag");
}
static void End(void *scene){
    // Zero delta marks this as a rotation, preventing a cancelled or
    // return-to-origin drag from being reinterpreted as a native click.
    rotateCodex(scene,0,deltaMethod);
    setDragging(scene,false,V2{},0,dragMethod);
    Log(@"codex_mouse drag=0 native_snap=1 virtual_touch=0");
}
static void Disable(void *scene){
    if(scene!=targetScene)return;
    if(gesture.cancel()&&setDragging)End(scene);
    targetScene=capturedPage=nullptr;
}
static void Frame(void *scene){
    bool held=(NativeBridge::physicalMouseSnapshot.load()&1)!=0;
    bool fresh=held&&!gesture.pointer.motion.held;
    uint64_t focus=NativeBridge::mapFocusRevision.load();
    if(targetScene&&scene!=targetScene)return;
    if(!targetScene){
        if(!fresh){gesture.sample(held,false,false,focus,0,0,dragThresholdPoints);return;}
        if(![Name(Property(scene,"get_gameObject"))isEqualToString:@"CharacterViewScene"])return;
    }
    bool active=NativeBridge::nativeDesktopMouseUI.load()&&!NativeBridge::nativeOverlayActive&&
        !NativeBridge::controllerInputActive.load()&&NativeBridge::IsForeground()&&!pointerCaptureSuspended&&WantsVisibleCursor();
    int w=NativeBridge::screenWidth?NativeBridge::screenWidth(NativeBridge::screenWidthMethod):0;
    int h=NativeBridge::screenHeight?NativeBridge::screenHeight(NativeBridge::screenHeightMethod):0;
    V2 p{nativeInputManager?*(float *)(nativeInputManager+0xd0):NAN,nativeInputManager?*(float *)(nativeInputManager+0xd4):NAN};
    CGRect bounds{};
    bool inside=w>0&&h>0&&p.x>=0&&p.y>=0&&p.x<w&&p.y<h&&GameContentBounds(bounds);
    bool mayBegin=false;
    if(fresh&&active&&inside&&Surface()){
        capturedPage=VisiblePage();
        mayBegin=capturedPage&&Bind(scene);
        if(mayBegin){
            targetScene=scene;sceneIdentity=Identity(scene);
            renderWidth=w;renderHeight=h;logicalWidth=bounds.size.width;logicalHeight=bounds.size.height;
        }
    }
    bool available=targetScene&&active&&inside&&capturedPage&&pageActive(capturedPage,activeMethod)&&
        w==renderWidth&&h==renderHeight&&fabs(bounds.size.width-logicalWidth)<.01&&
        fabs(bounds.size.height-logicalHeight)<.01&&Identity(scene)==sceneIdentity;
    if(!held&&available&&!gesture.nativeDragging&&!Surface())available=false;
    float x=CharacterMouseUnits::fromPixels(p.x,logicalWidth,renderWidth,1);
    float y=CharacterMouseUnits::fromPixels(p.y,logicalHeight,renderHeight,1);
    auto step=gesture.sample(held,available,mayBegin,focus,x,y,dragThresholdPoints);
    if(step.begin&&setDragging){
        setDragging(scene,true,p,0,dragMethod);
        Log(@"codex_mouse drag=1 native_carousel=1 source=mouse_points virtual_touch=0");
    }
    if(step.update&&rotateCodex)rotateCodex(scene,step.deltaX*mouseSensitivityX*carouselGain,deltaMethod);
    if(step.end&&setDragging)End(scene);
    if(step.click){
        click(scene,p,clickMethod);
        Log(@"codex_mouse native_click=1 position=(%.1f,%.1f) virtual_touch=0",p.x,p.y);
    }
    if(!gesture.pointer.motion.armed)targetScene=capturedPage=nullptr;
}
}
