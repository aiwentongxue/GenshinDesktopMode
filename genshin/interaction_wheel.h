#pragma once
#include <atomic>
#include <mutex>

// Both clients expose the same named interaction component and list fields.
// Resolve their actual metadata instead of relying on regional field offsets.
// No per-frame scene search, gameplay mutation, selection or F key synthesis.
namespace InteractionWheel {
using namespace SceneUIProbe;
static void *panelType,*panelClass,*finder;
static size_t pickupOffset,dialogOffset;
static std::atomic<bool> ready{false};
static std::mutex resolutionMutex;
static bool visible;
static double lastCheck=-100;
static bool Resolve() {
    if(ready)return true;
    if(!DesktopControlPreference::bootstrapReady.load() || NativeBridge::frames.load()<100)return false;
    std::lock_guard<std::mutex> guard(resolutionMutex);
    if(ready)return true;
    panelClass=findClass("Assembly-CSharp.dll","MoleMole","MonoInteePanel");
    if(!panelClass ||
       !NativeChatFocus::FieldOffset(panelClass,"_pickupPanel","MonoReusableList",pickupOffset) ||
       !NativeChatFocus::FieldOffset(panelClass,"_dialogPanel","MonoReusableList",dialogOffset))return false;
    panelType=TeleportCursorBridge::TypeObject("Assembly-CSharp.dll","MoleMole","MonoInteePanel");
    finder=TeleportCursorBridge::OneTypeArgument(findClass("UnityEngine.CoreModule.dll","UnityEngine","Object"),"FindObjectsOfType");
    ready=panelType&&finder;
    if(ready)Log(@"interaction_wheel resolved=1 profile=%s native_lists=1",RuntimeProfileName());
    return ready;
}
static unsigned Rows(void *list) {
    if(!list || strcmp(className(*(void **)list),"MonoReusableList") || !TeleportCursorBridge::Active(list))return 0;
    void *transform=Property(list,"get_transform");
    void *box=Property(transform,"get_childCount");
    int count=box?*(int *)((char *)box+16):0;
    if(count<0||count>128)return 0;
    void *get=transform?lookup(*(void **)transform,"GetChild",1,0):nullptr;
    unsigned active=0;
    for(int i=0;get&&i<count&&active<2;++i) {
        void *args[]={&i},*child=Call(get,transform,args);
        if(Bool(Property(child,"get_gameObject"),"get_activeInHierarchy"))++active;
    }
    return active;
}
static bool Context() {
    // Unity components may only be reflected on the main thread. Callers
    // from the engine input worker fail closed instead of touching managed
    // objects there; the scroll event path snapshots this on the main queue.
    if(!NSThread.isMainThread) {
        static bool reported=false;
        if(!reported){reported=true;Log(@"interaction_wheel refused_off_main_thread=1");}
        return false;
    }
    const double now=CACurrentMediaTime();
    // Only called with physical wheel input in captured world view. Bound
    // reflection during a rapid wheel burst; a new gesture checks immediately.
    if(now>=lastCheck&&now-lastCheck<.05)return visible;
    lastCheck=now;
    bool active=false;unsigned rows=0;uint64_t count=0;
    if(Resolve()) {
        void *args[]={panelType},*array=Call(finder,nullptr,args);
        count=array?*(uint64_t *)((char *)array+24):0;
        if(count<=8)for(uint64_t i=0;i<count;++i) {
            void *panel=((void **)((char *)array+32))[i];
            if(!panel||*(void **)panel!=panelClass||!TeleportCursorBridge::Active(panel))continue;
            rows=Rows(*(void **)((char *)panel+pickupOffset))+Rows(*(void **)((char *)panel+dialogOffset));
            if(rows>1){active=true;break;}
        }
    }
    if(active!=visible)Log(@"interaction_wheel discrete=%d active_rows=%u smoothing=%d",active,rows,!active);
    static unsigned samples=0;
    if(++samples<=6)Log(@"interaction_wheel context_sample=%u caller_main=%d panels=%llu active_rows=%u discrete=%d",
        samples,NSThread.isMainThread,count,rows,active);
    visible=active;return active;
}
static void Install() {
    if(!IsModernRuntime())return;
    NativeBridge::interactionWheelContext=Context;
    // Metadata discovery is unsafe during the early constructor phase. Use
    // the same input/bootstrap gate as the native controller resource adapter.
    __block unsigned attempts=0;
    [NSTimer scheduledTimerWithTimeInterval:2 repeats:YES block:^(NSTimer *timer){
        if(Resolve() || ++attempts>=90)[timer invalidate];
    }];
    Log(@"interaction_wheel installed=1 physical_notch=1 world_selection_only=1");
}
}
