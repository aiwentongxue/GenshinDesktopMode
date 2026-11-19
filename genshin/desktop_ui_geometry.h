#pragma once
// Native console overscan is applied independently to page and dialog layers.
// Restore observed controller padding or the verified console overscan pair
// under a unique root canvas. New modal-layer instances may replace old ones.
namespace DesktopUIGeometry {
using namespace SceneUIProbe;
struct V2 {float x,y;};
struct Padding {V2 min,max;int identity;};
struct Layer {const char *path;Padding keyboard{},controller{};bool haveKeyboard=false,haveController=false;};
static Layer layers[]={{"Pages"},{"Dialogs"},{"Dialogs/DialogLayer(Clone)"},{"SpecialDialogs"},{"SuspendBars"},{"Layer2"}};
static int rootIdentity;static uint64_t revision,restoreTicket;
static bool KeyboardReady(){
    if(NativeBridge::frames.load()<120||!DesktopControlPreference::bootstrapReady.load())return false;
    char *input=VerifiedInputOwner(),*ui=VerifiedUIOwner();
    return input&&ui&&*(int *)(input+GenshinOffsets().ownerInputMode)==2&&*(int *)(ui+GenshinOffsets().uiLayout)==1;
}
static bool Vector(void *object,const char *property,V2 &v){
    void *box=Property(object,property);if(!box)return false;memcpy(&v,(char *)box+16,sizeof(v));
    return std::isfinite(v.x)&&std::isfinite(v.y)&&fabs(v.x)<4096&&fabs(v.y)<4096;
}
static bool Same(V2 a,V2 b){return fabs(a.x-b.x)<.02f&&fabs(a.y-b.y)<.02f;}
static void *Child(void *root,const char *path){
    void *text=EngineFunction<void *(*)(const char *)>(0x10e9bb794)(path),*args[]={text};
    return root?Call(lookup(*(void **)root,"Find",1,0),root,args):nullptr;
}
static int Identity(void *object){void *id=Property(object,"GetInstanceID");return id?*(int *)((char *)id+16):0;}
static bool ReadTarget(void *target,Padding &p){
    if(!target||strcmp(className(*(void **)target),"RectTransform"))return false;
    V2 a,b;if(!Vector(target,"get_anchorMin",a)||!Vector(target,"get_anchorMax",b)||!Same(a,{0,0})||!Same(b,{1,1}))return false;
    if(!Vector(target,"get_offsetMin",p.min)||!Vector(target,"get_offsetMax",p.max))return false;
    p.identity=Identity(target);return p.identity!=0;
}
static void *Root(){
    void *type=TeleportCursorBridge::TypeObject("UnityEngine.UIModule.dll","UnityEngine","Canvas");
    void *resources=findClass("UnityEngine.CoreModule.dll","UnityEngine","Resources");
    void *find=TeleportCursorBridge::OneTypeArgument(resources,"FindObjectsOfTypeAll"),*args[]={type};
    void *array=type&&find?Call(find,nullptr,args):nullptr;uint64_t count=array?*(uint64_t *)((char *)array+24):0;
    if(!count||count>4096)return nullptr;void *matched=nullptr;
    for(uint64_t i=0;i<count;++i){
        void *canvas=((void **)((char *)array+32))[i];
        if(!canvas||!Bool(canvas,"get_isActiveAndEnabled")||!Bool(canvas,"get_isRootCanvas")||![Name(canvas)isEqualToString:@"Canvas"])continue;
        void *root=Property(canvas,"get_transform"),*pages=Child(root,"Pages");Padding p;
        // Modal chat/friend pages may hide Pages while Dialogs is active.
        if(!ReadTarget(pages,p))continue;
        if(matched)return nullptr;matched=root;
    }
    return matched;
}
// Retain this diagnostic reader for comparison with the original R44 evidence.
static bool Read(Padding &p,void **target=nullptr){
    void *root=Root(),*pages=Child(root,"Pages");if(!ReadTarget(pages,p))return false;
    if(target)*target=pages;return true;
}
static void Before(int previous,int requested){
    if(previous==requested)return;++revision;
    char *ui=VerifiedUIOwner();if(!ui)return;int layout=*(int *)(ui+GenshinOffsets().uiLayout);
    bool keyboard=previous==2&&requested==3&&layout==1;
    bool controller=previous==3&&requested==2&&layout==2;
    if(!keyboard&&!controller)return;void *root=Root();int id=Identity(root);if(!id)return;
    if(keyboard){rootIdentity=id;for(auto &layer:layers){layer.haveKeyboard=false;layer.haveController=false;}}
    if(id!=rootIdentity)return;
    for(auto &layer:layers){Padding p;if(!ReadTarget(Child(root,layer.path),p))continue;
        if(keyboard){
            bool console=Same(p.min,{68,32})&&Same(p.max,{-68,-32});
            if(!console){layer.keyboard=p;layer.haveKeyboard=true;}
        }
        else if(layer.haveKeyboard){layer.controller=p;layer.haveController=true;}
    }
}
static void RepairRoot(void *root){
    int id=Identity(root);if(!id)return;
    if(id&&id!=rootIdentity){rootIdentity=id;for(auto &layer:layers){layer.haveKeyboard=false;layer.haveController=false;}}
    for(auto &layer:layers){
        void *target=Child(root,layer.path);Padding current;if(!ReadTarget(target,current))continue;
        // A preconnected pad or newly instantiated dialog has no keyboard
        // snapshot. Only the verified console overscan pair is eligible for
        // the full-stretch desktop baseline; preserve other custom margins.
        bool console=Same(current.min,{68,32})&&Same(current.max,{-68,-32});
        bool stale=(layer.haveController&&Same(current.min,layer.controller.min)&&Same(current.max,layer.controller.max))||console;
        if(!layer.haveKeyboard&&!console){layer.keyboard=current;layer.haveKeyboard=true;continue;}
        Padding wanted=layer.haveKeyboard?layer.keyboard:Padding{{0,0},{0,0},0};
        bool done=Same(current.min,wanted.min)&&Same(current.max,wanted.max);
        if(!stale||done)continue;
        void *setMin=lookup(*(void **)target,"set_offsetMin",1,0),*setMax=lookup(*(void **)target,"set_offsetMax",1,0);
        if(!setMin||!setMax)continue;void *a[]={&wanted.min},*b[]={&wanted.max};
        Call(setMin,target,a);Call(setMax,target,b);Padding check;
        bool ok=ReadTarget(target,check)&&Same(check.min,wanted.min)&&Same(check.max,wanted.max);
        if(ok){layer.keyboard=check;layer.haveKeyboard=true;}
        Log(@"desktop_ui_padding restored_keyboard=%d role=%s observed_console=%d",ok,layer.path,console);
    }
}
static void (*originalAdapt)(void *,V2,V2,void *);
static void Restore(uint64_t expected,unsigned attempt){
    if(expected!=revision||!KeyboardReady())return;
    RepairRoot(Root());
    // Keep a bounded fallback for clients whose adapter is unavailable. Only
    // one restore chain may survive a successful new device handoff.
    uint64_t ticket=restoreTicket;
    if(!originalAdapt && attempt<10)dispatch_after(dispatch_time(DISPATCH_TIME_NOW,100*NSEC_PER_MSEC),dispatch_get_main_queue(),^{
        if(ticket==restoreTicket)Restore(expected,attempt+1);
    });
}
static void After(int actual){++restoreTicket;if(actual==2)Restore(revision,0);}
// The native screen adapter calls this only when applying its geometry or
// discovering a new page, including modals created after the retry window.
// Follow that event, not a per-frame Resources/scene scan.
static void AdapterFinished(void *adapter){
    if(!adapter||!KeyboardReady())return;
    void *transform=Property(adapter,"get_transform"),*root=Property(transform,"get_root");
    Padding page;
    if(!root||![Name(root)isEqualToString:@"Canvas"]||Property(root,"get_parent")||
       !ReadTarget(Child(root,"Pages"),page))return;
    RepairRoot(root);
}
static void Adapt(void *adapter,V2 min,V2 max,void *method){
    originalAdapt(adapter,min,max,method);
    AdapterFinished(adapter);
}
static void Install(){
    if(originalAdapt||!IsModernRuntime())return;
    void *cls=findClass("Assembly-CSharp.dll","","MonoUIScreenAdapter");
    void *method=cls?NativeCameraPointer::Method(cls,"TryAffectUIContexts","Void",{"Vector2","Vector2"}):nullptr;
    void *target=method?(void *)ManagedMethodCode(method):nullptr;
    if(!target||!NativeCameraPointer::RegisterOnlyPrologue(target)){
        Log(@"desktop_ui_padding native_refresh=0 fallback=bounded_restore profile=%s",RuntimeProfileName());return;
    }
    auto trampoline=(decltype(originalAdapt))NativeCameraPointer::Trampoline(target);if(!trampoline)return;
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)Adapt);
    originalAdapt=trampoline;
    if(!NativeBridge::PatchCode(target,jump,16)){
        originalAdapt=nullptr;munmap((void *)trampoline,4096);return;
    }
    Log(@"desktop_ui_padding native_refresh=1 late_dialogs=1 scene_poll=0 profile=%s",RuntimeProfileName());
}
}
