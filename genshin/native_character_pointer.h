// Character preview consumes mouse deltas through its own model controller.
// This adapter never creates a Touch/Gesture or modifies cursor/web routing.
#include "character_mouse_units.h"
#include "character_pointer_state.h"
#include "drag_geometry.h"
namespace NativeCharacterPointer {
using namespace SceneUIProbe;
static void (*originalUpdate)(void *,void *),(*originalDisable)(void *,void *);
static void (*rotateModel)(void *,float,float,void *);
static bool (*setDragging)(void *,bool,NativeCameraPointer::Vector2,float,void *);
static bool (*pageActive)(void *,void *);
static float mouseSensitivityX=.1f,mouseSensitivityY=.1f;
// Slower character-preview response, including the user's follow-up adjustment.
// This changes model rotation only, not the system cursor or gameplay camera.
static constexpr float rotationGain=.20f;
static constexpr float dragThresholdPoints=5;
static double logicalWidth,logicalHeight;
static int renderWidth,renderHeight;
static DesktopDragGeometry capturedGeometry;
static void *rotateMethod,*dragMethod,*activeMethod,*pageType,*findPageMethod;
static void *capturedScene,*capturedPage;
static CharacterPointerState gesture;
static bool installed;

static void End(void *scene) {
    rotateModel(scene,0,0,rotateMethod);
    setDragging(scene,false,NativeCameraPointer::Vector2{},0,dragMethod);
    Log(@"character_mouse drag=0 native_model_rotation=1 virtual_touch=0");
}
static void *FindCharacterPage(void) {
    void *args[]={pageType},*array=Call(findPageMethod,nullptr,args);
    uint64_t count=array?*(uint64_t *)((char *)array+24):0;
    if(count>16) return nullptr;
    for(uint64_t i=0;i<count;++i) {
        void *page=((void **)((char *)array+32))[i];
        if(page&&pageActive(page,activeMethod)) return page;
    }
    return nullptr;
}
static bool ClearModelSurface(void) {
    char *owner=NativePC::InputOwner();
    void *module=owner?*(void **)(owner+GenshinOffsets().ownerModule):nullptr;
    void *pointer=module?NativePC::MousePointer(module):nullptr;
    // The character model has no UI raycast target. Preserve existing native
    // ownership for tabs, party portraits, buttons, scroll lists and dialogs.
    return pointer&&!Property(pointer,"get_pointerEnter")&&
        !Property(pointer,"get_pointerPress")&&!Property(pointer,"get_pointerDrag");
}
static void MouseFrame(void *scene) {
    if(capturedScene&&scene!=capturedScene) return;
    bool held=(NativeBridge::physicalMouseSnapshot.load()&1)!=0;
    if(!held&&!gesture.motion.held&&!capturedScene) {
        gesture.sample(false,false,false,NativeBridge::mapFocusRevision.load(),0,0,dragThresholdPoints);
        return;
    }
    bool foreground=!pointerCaptureSuspended&&NativeBridge::IsForeground();
    bool enabled=NativeBridge::nativeDesktopMouseUI.load()&&
        !NativeBridge::nativeOverlayActive&&foreground&&WantsVisibleCursor();
    if(!enabled&&!capturedScene) {
        gesture.sample(held,false,false,NativeBridge::mapFocusRevision.load(),0,0,dragThresholdPoints);
        return;
    }
    float x=nativeInputManager?*(float *)(nativeInputManager+0xd0):0;
    float y=nativeInputManager?*(float *)(nativeInputManager+0xd4):0;
    int w=NativeBridge::ScreenWidthValue();
    int h=NativeBridge::ScreenHeightValue();
    CGRect bounds{};
    bool inside=w>0&&h>0&&x>=0&&y>=0&&x<w&&y<h&&GameContentBounds(bounds);
    bool fresh=held&&!gesture.motion.held;
    bool mayBegin=false;
    if(fresh&&enabled&&inside&&ClearModelSurface()&&
       [Name(Property(scene,"get_gameObject")) isEqualToString:@"CharacterViewScene"]) {
        // One typed scene query on a new eligible press, never a per-frame
        // scene scan. Lifetime stays bounded by this active Unity callback.
        capturedPage=FindCharacterPage();
        mayBegin=capturedPage&&capturedGeometry.capture(w,h,bounds.size.width,bounds.size.height);
        if(mayBegin) {
            capturedScene=scene;
            logicalWidth=bounds.size.width;logicalHeight=bounds.size.height;
            renderWidth=w;renderHeight=h;
        }
    }
    bool available=enabled&&inside&&capturedPage&&pageActive(capturedPage,activeMethod)&&
        capturedGeometry.matches(w,h,bounds.size.width,bounds.size.height);
    // Use system points for the click/drag threshold at every render scale.
    // Rotation and native picking continue to receive their respective units.
    float pointX=logicalWidth>0&&renderWidth>0?x*logicalWidth/renderWidth:NAN;
    float pointY=logicalHeight>0&&renderHeight>0?y*logicalHeight/renderHeight:NAN;
    if(!held&&available&&!ClearModelSurface()) available=false;
    auto event=gesture.sample(held,available,mayBegin,NativeBridge::mapFocusRevision.load(),
        pointX,pointY,dragThresholdPoints);
    auto step=event.motion;
    if(event.pressed) {
        // The native method resets its previous-rotation flag on press.
        // Without this, a later click can still be treated as the old drag.
        setDragging(scene,true,NativeCameraPointer::Vector2{x,y},0,dragMethod);
    }
    if(step.ended) End(scene);
    if(event.cancelled) End(scene);
    if(event.clicked) {
        // SetOnDrag(false, screenPosition, 0) is the game's own 3D relic
        // raycast/selection route. Never invoke it after a rotation or cancel.
        bool selected=setDragging(scene,false,NativeCameraPointer::Vector2{x,y},0,dragMethod);
        Log(@"character_mouse click=1 native_scene_pick=1 selected=%d virtual_touch=0",selected);
    }
    if(step.began) {
        Log(@"character_mouse drag=1 native_model_rotation=1 source=unity_mouse_pixels virtual_touch=0");
    }
    if(gesture.motion.dragging) {
        // This mobile page reports zero for Input.GetAxis despite real mouse
        // movement. The state already uses system points; apply the client's
        // original InputManager axis units once, independently of render scale.
        float dx=step.moved?step.x*mouseSensitivityX:0;
        float dy=step.moved?step.y*mouseSensitivityY:0;
        dx*=rotationGain;dy*=rotationGain;
        if(!std::isfinite(dx)||!std::isfinite(dy)) dx=dy=0;
        rotateModel(scene,dx,dy,rotateMethod);
        static uint64_t packets=0;
        if(step.moved&&(++packets<=3||packets%240==0))
            Log(@"character_mouse_delta packet=%llu points=(%.2f,%.2f) axes=(%.3f,%.3f) preview_gain=%.2f",packets,step.x,step.y,dx,dy,rotationGain);
    }
    if(!gesture.motion.armed&&!gesture.motion.dragging) capturedScene=capturedPage=nullptr;
}
static void Update(void *scene,void *method) {
    NativeCodexPointer::Frame(scene);
    MouseFrame(scene);
    originalUpdate(scene,method);
}
static void Disable(void *scene,void *method) {
    NativeCodexPointer::Disable(scene);
    if(scene==capturedScene) {
        if(gesture.cancel()) End(scene);
        capturedScene=capturedPage=nullptr;
    }
    originalDisable(scene,method);
}
static bool TryInstall(void) {
    if(installed) return true;
    if(!NativeBridge::enabled||!NativeBridge::frames.load()) return false;
    void *scene=findClass("Assembly-CSharp.dll","MoleMole","MonoSceneUISetup");
    void *page=findClass("Assembly-CSharp.dll","MoleMole","MonoCharacterPage");
    if(!scene||!page) return false;
    auto method=NativeCameraPointer::Method;
    void *update=method(scene,"Update","Void",{}),*disable=method(scene,"OnDisable","Void",{});
    rotateMethod=method(scene,"RotateModelByDelta","Void",{"Single","Single"});
    dragMethod=method(scene,"SetOnDrag","Boolean",{"Boolean","Vector2","Single"});
    activeMethod=method(page,"get_isActiveAndEnabled","Boolean",{});
    pageType=TeleportCursorBridge::TypeObject("Assembly-CSharp.dll","MoleMole","MonoCharacterPage");
    findPageMethod=TeleportCursorBridge::OneTypeArgument(
        findClass("UnityEngine.CoreModule.dll","UnityEngine","Object"),"FindObjectsOfType");
    if(!update||!disable||!rotateMethod||!dragMethod||!activeMethod||!pageType||!findPageMethod) {
        Log(@"character_mouse refused_methods=1");return false;
    }
    NSString *settings=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Data/globalgamemanagers"];
    NSData *asset=[NSData dataWithContentsOfFile:settings options:NSDataReadingMappedIfSafe error:nil];
    bool parsedX=asset&&CharacterMouseUnits::sensitivity((const uint8_t *)asset.bytes,asset.length,"Mouse X",0,mouseSensitivityX);
    bool parsedY=asset&&CharacterMouseUnits::sensitivity((const uint8_t *)asset.bytes,asset.length,"Mouse Y",1,mouseSensitivityY);
    Log(@"character_mouse_units input_manager=(%d,%d) sensitivity=(%.3f,%.3f) render_scale_removed=1 preview_gain=%.2f",parsedX,parsedY,mouseSensitivityX,mouseSensitivityY,rotationGain);
    void *targets[]={(void *)ManagedMethodCode(update),(void *)ManagedMethodCode(disable)};
    uint8_t saved[2][16];
    for(unsigned i=0;i<2;++i) {
        if(!NativeCameraPointer::RegisterOnlyPrologue(targets[i])) {
            Log(@"character_mouse refused_prologue=%u",i);return false;
        }
        memcpy(saved[i],targets[i],16);
    }
    originalUpdate=(decltype(originalUpdate))NativeCameraPointer::Trampoline(targets[0]);
    originalDisable=(decltype(originalDisable))NativeCameraPointer::Trampoline(targets[1]);
    if(!originalUpdate||!originalDisable) return false;
    rotateModel=(decltype(rotateModel))ManagedMethodCode(rotateMethod);
    setDragging=(decltype(setDragging))ManagedMethodCode(dragMethod);
    pageActive=(decltype(pageActive))ManagedMethodCode(activeMethod);
    void *replacements[]={(void *)Update,(void *)Disable};
    for(unsigned i=0;i<2;++i) {
        uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)replacements[i]);
        if(!NativeBridge::PatchCode(targets[i],jump,16)) {
            for(unsigned j=0;j<=i;++j) NativeBridge::PatchCode(targets[j],saved[j],16);
            return false;
        }
    }
    installed=true;
    Log(@"character_mouse installed=1 methods=runtime_metadata typed_page=MonoCharacterPage native_scene_pick=1 preview_gain=%.2f virtual_touch=0",rotationGain);
    return true;
}
}
