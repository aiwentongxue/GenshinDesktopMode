// Desktop mouse drives EasyTouch's map gestures, as in the original Star Rail
// module. Scope is explicit, positions are shared with Unity Mouse, and phases
// advance with engine frames instead of a fixed release timer.
#include "map_touch_state.h"
namespace NativeMapDrag {
using Touch=DesktopMapTouchState::Touch;
using Snapshot=DesktopMapTouchState::Snapshot;
static DesktopMapTouchState state;
static thread_local const Snapshot *reading;
static void (*originalUpdate)(void *,void *);
static void (*originalTouch)(int,Touch *);
static bool installed;
static double lastMapRefresh=-1;
static bool Ownership(bool refresh) {
    if(!installed || !NativeBridge::nativeDesktopMouseUI.load() || NativeBridge::nativeOverlayActive ||
       pointerCaptureSuspended || !WantsVisibleCursor() || !NativeBridge::IsForeground()) return false;
    double now=NSProcessInfo.processInfo.systemUptime;
    if(refresh && now-lastMapRefresh>0.02) {
        NativeBridge::mapPageVisible=TeleportCursorBridge::MapTouchContext();lastMapRefresh=now;
    }
    return NativeBridge::mapPageVisible.load();
}
static const Snapshot &Capture(void) {
    // Observe physical state even while an overlay suppresses game clicks.
    // Closing that overlay while still held must not create a fresh press.
    bool held=(NativeBridge::physicalMouseSnapshot.load()&1)!=0;
    static bool previousHeld;
    bool fresh=held && !previousHeld;previousHeld=held;
    bool context=Ownership(fresh);
    // These are the exact absolute pixel coordinates Unity Mouse reads this
    // frame, after native input update; no second acceleration or sensitivity.
    float x=*(float *)(nativeInputManager+0xd0),y=*(float *)(nativeInputManager+0xd4);
    int w=NativeBridge::screenWidth?NativeBridge::screenWidth(NativeBridge::screenWidthMethod):0;
    int h=NativeBridge::screenHeight?NativeBridge::screenHeight(NativeBridge::screenHeightMethod):0;
    bool inside=w>0 && h>0 && x>=0 && y>=0 && x<w && y<h;
    // A game may pause its updates while inactive. Deliver cancellation even
    // if no EasyTouch frame ran between losing focus and returning to the app.
    static uint64_t previousFocusRevision;
    auto revision=NativeBridge::mapFocusRevision.load();
    bool uninterrupted=revision==previousFocusRevision;previousFocusRevision=revision;
    return state.sample(NativeBridge::frames.load(),NSProcessInfo.processInfo.systemUptime,
        context,NativeBridge::IsForeground() && uninterrupted,held,inside,x,y);
}
static int TouchCount(void) {
    if(reading) return reading->count;
    // The verified original wrapper is exactly prologue / BL / epilogue.
    // Call its lower getter directly, avoiding relocation of that BL.
    return EngineFunction<int (*)()>(0x10eba1090)();
}
static void GetTouch(int index,Touch *out) {
    if(reading && reading->count==1 && index==0) {*out=reading->touch;return;}
    originalTouch(index,out);
}
static void Update(void *object,void *method) {
    const auto &snapshot=Capture();
    const Snapshot *saved=reading;
    bool own=Ownership(false) || snapshot.count!=0;
    reading=own?&snapshot:nullptr;
    @try {originalUpdate(object,method);} @finally {reading=saved;}
    static int previous=-1;
    int phase=snapshot.count?snapshot.touch.phase:-1;
    if(phase!=previous) {
        previous=phase;
        Log(@"native_map_drag phase=%d count=%d scope=%d frame=%llu",phase,snapshot.count,own,NativeBridge::frames.load());
    }
}
static void *Trampoline(uintptr_t address) {
    void *target=EngineFunction<void *>(address);
    if(!RuntimePrologueMatches(address,target)) return nullptr;
    // Both independently checked prologues consist of stack/register ops.
    void *code=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(code==MAP_FAILED) return nullptr;
    memcpy(code,target,16);NativeBridge::AbsoluteJump((char *)code+16,(uintptr_t)target+16);
    sys_icache_invalidate(code,32);
    if(mprotect(code,4096,PROT_READ|PROT_EXEC)) {munmap(code,4096);return nullptr;}
    return code;
}
static bool TryInstall(void) {
    if(installed) return true;
    if(!NativeBridge::enabled || !NativeBridge::frames.load()) return false;
    using namespace SceneUIProbe;
    void *easy=findClass("Assembly-CSharp-firstpass.dll","HedgehogTeam.EasyTouch","EasyTouch");
    void *input=findClass("UnityEngine.InputLegacyModule.dll","UnityEngine","Input");
    if(!input) input=findClass("UnityEngine.CoreModule.dll","UnityEngine","Input");
    void *touch=findClass("UnityEngine.InputLegacyModule.dll","UnityEngine","Touch");
    if(!touch) touch=findClass("UnityEngine.CoreModule.dll","UnityEngine","Touch");
    if(!easy || !input || !touch) {
        static bool warned=false;if(!warned) {warned=true;Log(@"native_map_drag waiting_types easy=%d input=%d touch=%d",easy!=nullptr,input!=nullptr,touch!=nullptr);}return false;
    }
    void *update=lookup(easy,"Update",0,0),*count=lookup(input,"get_touchCount",0,0),*get=lookup(input,"GetTouch",1,0);
    if(!update || !count || !get || ManagedMethodCode(update)!=(uintptr_t)EngineFunction<void *>(0x10023942c) ||
       ManagedMethodCode(count)!=(uintptr_t)EngineFunction<void *>(0x10e864284) ||
       ManagedMethodCode(get)!=(uintptr_t)EngineFunction<void *>(0x10e864288)) {
        static bool warned=false;if(!warned) {warned=true;Log(@"native_map_drag refused_methods update=%p count=%p get=%p",update?(void *)ManagedMethodCode(update):nullptr,count?(void *)ManagedMethodCode(count):nullptr,get?(void *)ManagedMethodCode(get):nullptr);}return false;
    }
    // Metadata offsets include the boxed value's two-word object header.
    if(!HasManagedField(touch,nullptr,16,"Int32") || !HasManagedField(touch,nullptr,20,"Vector2") ||
       !HasManagedField(touch,nullptr,36,"Vector2") || !HasManagedField(touch,nullptr,52,"TouchPhase") ||
       !HasManagedField(touch,nullptr,80,"Single")) {
        static bool warned=false;if(!warned) {warned=true;Log(@"native_map_drag refused_touch_abi");}return false;
    }
    struct Patch {uintptr_t address;void *replacement;uint8_t saved[16];};
    Patch patches[]={{0x10f8014dc,(void *)GetTouch,{}},{0x10f80155c,(void *)TouchCount,{}},
                     {0x10023942c,(void *)Update,{}}};
    for(auto &p:patches) {
        void *target=EngineFunction<void *>(p.address);
        if(!RuntimePrologueMatches(p.address,target)) return false;
        memcpy(p.saved,target,16);
    }
    originalTouch=(void (*)(int,Touch *))Trampoline(0x10f8014dc);
    originalUpdate=(void (*)(void *,void *))Trampoline(0x10023942c);
    if(!originalTouch || !originalUpdate) return false;
    for(unsigned i=0;i<3;++i) {
        uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)patches[i].replacement);
        if(!NativeBridge::PatchCode(EngineFunction<void *>(patches[i].address),jump,16)) {
            for(unsigned j=0;j<=i;++j) NativeBridge::PatchCode(EngineFunction<void *>(patches[j].address),patches[j].saved,16);
            return false;
        }
    }
    installed=true;NativeBridge::mapTouchOwnership=Ownership;
    Log(@"native_map_drag installed=1 source=unity_mouse_pixels scope=map_easytouch frame_snapshot=1 touch_abi=68");
    return true;
}
static void Install(void) {
    if(TryInstall()) return;
    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *timer) {
        static unsigned attempts=0;
        if(TryInstall() || ++attempts>=120) [timer invalidate];
    }];
}
}
