#pragma once
#include "control_device_profile.h"
#include "control_device_policy.h"

// Keep the game's real selector, controller discovery and stored control
// setting; expose keyboard/mouse in place of the mobile touch option.
namespace ControlDevice {
using namespace SceneUIProbe;
struct State {void *context,*row;int nativeCount;};
static std::vector<State> states;
static bool installed,installRefused;
static void *refreshInfo,*selectionInfo,*keyboardInfo;
static void (*originalRefresh)(void *,void *);
static void (*originalSelection)(void *,int,void *);
static void (*originalEventSelection)(void *,int,void *);
static void (*nativeKeyboard)(void *,void *);
static void (*originalShow)(void *,bool,void *);
static size_t rowOffset,optionsOffset;
static bool TryInstall(void *hint=nullptr);
static uint64_t refreshCalls;
static thread_local void *silentSelectionContext;
struct SilentSelection {
    void *previous;
    explicit SilentSelection(void *context):previous(silentSelectionContext){silentSelectionContext=context;}
    ~SilentSelection(){silentSelectionContext=previous;}
};
static void *failureContexts[10];
static bool AppendFailure(void *context,unsigned stage,const char *reason) {
    if(stage<10&&failureContexts[stage]!=context){failureContexts[stage]=context;Log(@"control_device append_deferred stage=%u reason=%s context=%p",stage,reason,context);}
    return false;
}

static bool VoidCall(void *method,void *object,void **args=nullptr) {
    if(!method)return false;
    void *exception=nullptr;invoke(method,object,args,&exception);
    if(exception)Log(@"control_device managed_exception=%s",className(*(void **)exception));
    return !exception;
}
static void *Method(void *cls,const char *name,const char *result,
                    std::initializer_list<const char *> parameters) {
    return NativeCameraPointer::Method(cls,name,result,parameters);
}
static void *MethodAt(void *cls,uintptr_t expected,const char *result,
                      std::initializer_list<const char *> parameters) {
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto count=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    auto returns=EngineFunction<void *(*)(void *)>(0x10e9bb6e8);
    auto parameter=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    void *it=nullptr;
    while(void *m=next(cls,&it)) {
        if(ManagedMethodCode(m)!=expected||count(m)!=parameters.size()||
           strcmp(className(from(returns(m))),result))continue;
        bool valid=true;unsigned i=0;
        for(const char *p:parameters)if(strcmp(className(from(parameter(m,i++))),p))valid=false;
        if(valid)return m;
    }
    return nullptr;
}
static int CurrentMode(void) {
    char *owner=VerifiedInputOwner();return owner?*(int *)(owner+GenshinOffsets().ownerInputMode):-1;
}
static bool ContextLayout(void *cls) {
    return cls&&!strcmp(className(cls),"ABOBEFKCDJB")&&
        HasManagedField(cls,nullptr,rowOffset,"MonoDropdownSetting")&&
        HasManagedField(cls,nullptr,optionsOffset,"List`1");
}
static State *StateFor(void *context) {
    for(auto &s:states)if(s.context==context)return &s;return nullptr;
}
static void *StringListAddOptions(void *cls) {
    auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
    auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
    auto name=EngineFunction<const char *(*)(void *)>(0x10e9bb6f0);
    auto count=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
    auto parameter=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
    auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
    for(unsigned depth=0;cls&&depth<8;cls=parent(cls),++depth) {
        void *it=nullptr;
        while(void *m=next(cls,&it)) {
            if(strcmp(name(m),"AddOptions")||count(m)!=1||!ManagedMethodCode(m))continue;
            void *list=from(parameter(m,0));
            if(list&&!strcmp(className(list),"List`1")&&HasManagedField(list,nullptr,16,"String[]"))return m;
        }
    }
    return nullptr;
}
static bool RebuildDesktopOptions(void *context) {
    if(!context||!ContextLayout(*(void **)context))return AppendFailure(context,0,"context_layout");
    void *row=*(void **)((char *)context+rowOffset);
    void *list=*(void **)((char *)context+optionsOffset);
    if(!row||strcmp(className(*(void **)row),"MonoDropdownSetting")||!list||
       strcmp(className(*(void **)list),"List`1")||!HasManagedField(*(void **)list,nullptr,16,"String[]")){
        Log(@"control_device append_types row=%s list=%s string_items=%d",row?className(*(void **)row):"null",list?className(*(void **)list):"null",list?HasManagedField(*(void **)list,nullptr,16,"String[]"):0);
        return AppendFailure(context,1,"row_or_string_list");
    }
    void *dropdown=Property(row,"get_dropdown");
    if(!dropdown||strcmp(className(*(void **)dropdown),"MonoDropdownExtention"))return AppendFailure(context,2,"dropdown");
    void *box=Property(list,"get_Count");int count=box?*(int *)((char *)box+16):0;
    // Native refresh has just recreated touch plus a detected controller, if
    // available. Preserve that capability decision and the native index 1.
    // Do not add a disconnected controller or reinterpret future option sets.
    if(!DesktopControlDevicePolicy::KnownOptions(count)){Log(@"control_device native_options_count=%d",count);return AppendFailure(context,3,"native_options_count");}
    void *replace=Method(*(void **)list,"set_Item","Void",{"Int32","String"});
    void *clear=Method(*(void **)dropdown,"ClearOptions","Void",{});
    void *addOptions=StringListAddOptions(*(void **)dropdown);
    void *setValue=Method(*(void **)dropdown,"SetValueWithoutNotify","Void",{"Int32"});
    void *setInternal=setValue?nullptr:Method(*(void **)dropdown,"Set","Void",{"Int32","Boolean"});
    void *setNotifying=(!setValue&&!setInternal)?Method(*(void **)dropdown,"set_value","Void",{"Int32"}):nullptr;
    if(!replace||!clear||!addOptions||(!setValue&&!setInternal&&!setNotifying)){
        Log(@"control_device rebuild_methods replace=%d clear=%d strings=%d silent_value=%d internal_set=%d value_setter=%d",replace!=nullptr,clear!=nullptr,addOptions!=nullptr,setValue!=nullptr,setInternal!=nullptr,setNotifying!=nullptr);
        return AppendFailure(context,4,"methods");
    }
    void *text=EngineFunction<void *(*)(const char *)>(0x10e9bb794)("键鼠设备");
    if(!text)return AppendFailure(context,5,"string");
    int keyboardIndex=0;void *replaceArgs[]={&keyboardIndex,text};
    if(!VoidCall(replace,list,replaceArgs))return false;
    if(!VoidCall(clear,dropdown))return false;
    void *optionsArgs[]={list};if(!VoidCall(addOptions,dropdown,optionsArgs))return false;
    State *state=StateFor(context);
    if(!state){if(states.size()>=16)states.erase(states.begin());states.push_back({context,row,count});state=&states.back();}
    else *state={context,row,count};
    int selected=DesktopControlDevicePolicy::SelectedIndex(count,CurrentMode());
    bool notify=false;void *valueArgs[]={&selected,&notify};
    {
        // This client's Unity UI has only the ordinary value setter. Let its
        // real setter clamp/store/render, while preventing this presentation
        // synchronization from becoming a control-device selection action.
        SilentSelection synchronize(context);
        if(!VoidCall(setValue?setValue:setInternal?setInternal:setNotifying,dropdown,valueArgs))return false;
    }
    Log(@"control_device options visible=%d touch=0 keyboard_index=0 controller_index=%d selected=%d mode=%d native_config=1",
        count,count==2?1:-1,selected,CurrentMode());
    return true;
}
static void Refresh(void *context,void *method) {
    ++refreshCalls;if(refreshCalls<=4)Log(@"control_device native_refresh_hit=%llu context=%p",refreshCalls,context);
    {SilentSelection synchronize(context);originalRefresh(context,method);}
    RebuildDesktopOptions(context);
}
static void Selection(void *context,int index,void *method) {
    if(silentSelectionContext==context) return;
    State *state=StateFor(context);
    bool valid=state&&ContextLayout(*(void **)context)&&*(void **)((char *)context+rowOffset)==state->row;
    int requested=valid?DesktopControlDevicePolicy::InputMode(state->nativeCount,index):-1;
    if((requested==2||requested==3)&&DesktopControlPreference::explicitSelection)
        DesktopControlPreference::explicitSelection(requested);
    if(requested==2) {
        // This native method writes the real PC control preference and switches
        // the game's input owner. The original callback's -1 branch performs
        // its shared UI rebuild without re-selecting touch/controller.
        nativeKeyboard(context,keyboardInfo);
        originalSelection(context,-1,method);
        if(!DesktopUI::SwitchInputMode(2)) DesktopUI::QueueDeviceLayoutRepair(2);
        DesktopControlPreference::RememberSelection(2,CurrentMode());
        Refresh(context,refreshInfo);
        Log(@"control_device selected=keyboard_mouse input=%d stored_by_native=1",CurrentMode());
        return;
    }
    // Never let visible keyboard index 0 fall through to native touch index 0,
    // including an out-of-date or unexpected callback.
    if(requested!=3) {Log(@"control_device ignored_stale_selection index=%d",index);Refresh(context,refreshInfo);return;}
    originalSelection(context,1,method);
    int mode=CurrentMode();
    if((mode==2||mode==3)&&!DesktopUI::SwitchInputMode(mode))
        DesktopUI::QueueDeviceLayoutRepair(mode);
    DesktopControlPreference::RememberSelection(3,CurrentMode());
    // The original handler rebuilds groups asynchronously in some contexts.
    // Refreshing through its own method reattaches its existing callbacks.
    Refresh(context,refreshInfo);
}
static void EventSelection(void *context,int index,void *method) {
    // The game's public Dropdown setter first closes the settings page in
    // this wrapper, before reaching the device callback. Presentation-only
    // value synchronization must bypass the entire wrapper, not just its
    // final selection action, or the settings page is left hidden.
    if(silentSelectionContext==context) return;
    originalEventSelection(context,index,method);
}
static void *CurrentContext(void) {
    void *go=findClass("UnityEngine.CoreModule.dll","UnityEngine","GameObject");
    void *find=go?Method(go,"Find","GameObject",{"String"}):nullptr;
    if(!find)return nullptr;
    void *name=EngineFunction<void *(*)(const char *)>(0x10e9bb794)("SettingPage");
    void *args[]={name},*object=Call(find,nullptr,args);if(!object)return nullptr;
    void *type=TeleportCursorBridge::TypeObject("Assembly-CSharp.dll","MoleMole","MonoSettingPage");
    void *get=TeleportCursorBridge::OneTypeArgument(*(void **)object,"GetComponent");
    void *componentArgs[]={type};void *page=type&&get?Call(get,object,componentArgs):nullptr;
    if(!page||!HasManagedField(*(void **)page,"CPCIBFHDIBB",24,"AEHEPABFEBB"))return nullptr;
    void *context=*(void **)((char *)page+24);
    return context&&ContextLayout(*(void **)context)?context:nullptr;
}
static void *PointerField(void *object,const char *name,size_t offset,const char *type) {
    return object&&HasManagedField(*(void **)object,name,offset,type)?
        *(void **)((char *)object+offset):nullptr;
}
static void *ActionContext(void *action,void *dropdown) {
    for(unsigned depth=0;action&&depth<4;++depth) {
        void *target=PointerField(action,"m_target",32,"Object");if(!target)return nullptr;
        if(ContextLayout(*(void **)target)) {
            void *row=*(void **)((char *)target+rowOffset);
            return row&&Property(row,"get_dropdown")==dropdown?target:nullptr;
        }
        // The real native callback is wrapped once in MEJFAENGGIJ. Its only
        // managed field is the original UnityAction<int>, at object offset16.
        action=PointerField(target,nullptr,16,"UnityAction`1");
    }
    return nullptr;
}
static void *DropdownContext(void *dropdown) {
    void *event=PointerField(dropdown,"m_OnValueChanged",272,"DropdownEvent");
    void *calls=PointerField(event,"m_Calls",16,"InvokableCallList");
    void *list=PointerField(calls,"m_RuntimeCalls",24,"List`1");if(!list)return nullptr;
    void *box=Property(list,"get_Count");int count=box?*(int *)((char *)box+16):0;
    if(count<0||count>32)return nullptr;
    void *get=lookup(*(void **)list,"get_Item",1,0);
    for(int i=0;i<count;++i){void *args[]={&i};void *item=Call(get,list,args);
        void *action=PointerField(item,"Delegate",16,"UnityAction`1");
        if(void *context=ActionContext(action,dropdown))return context;
    }
    return nullptr;
}
static void Show(void *dropdown,bool force,void *method) {
    // A pooled setting row may have been populated before input readiness.
    // Derive its context from the real event target at popup-open time; no
    // scene search, object-name dependency or per-frame polling is involved.
    void *context=DropdownContext(dropdown);
    static unsigned shows=0;if(++shows<=4)Log(@"control_device native_popup_hit=%u context=%p",shows,context);
    if(context) {
        if(!installed)TryInstall(context);
        if(installed){Log(@"control_device selector_open context=%p",context);Refresh(context,refreshInfo);}
    }
    originalShow(dropdown,force,method);
}
static bool InstallShow(void) {
    if(originalShow)return true;
    void *cls=findClass("Assembly-CSharp.dll","MoleMole","MonoDropdownExtention");
    if(!cls||!HasManagedField(cls,"m_Value",256,"Int32")||
       !HasManagedField(cls,"m_Options",264,"OptionDataList"))return false;
    // Unlike the obfuscated setting helpers, this is a named, typed native UI
    // API. Match its metadata contract and a relocatable register-only entry.
    void *method=Method(cls,"Show","Void",{"Boolean"});
    void *target=(void *)ManagedMethodCode(method);if(!target)return false;
    originalShow=(void (*)(void *,bool,void *))NativeCameraPointer::Trampoline(target);
    if(!originalShow)return false;
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)Show);
    if(!NativeBridge::PatchCode(target,jump,16)){originalShow=nullptr;return false;}
    Log(@"control_device selector_open_hook=1 address=0x%llx",(uintptr_t)target-_dyld_get_image_vmaddr_slide(0));
    return true;
}
static bool TryInstall(void *hint) {
    if(installed||installRefused)return installed;
    bool global=IsGlobalModernRuntime();
    if(!IsModernRuntime())return false;
    rowOffset=global?ControlDeviceProfile::GlobalRow:ControlDeviceProfile::CNRow;
    optionsOffset=global?ControlDeviceProfile::GlobalOptions:ControlDeviceProfile::CNOptions;
    void *current=hint,*cls=hint&&ContextLayout(*(void **)hint)?*(void **)hint:findClass("Assembly-CSharp.dll","MoleMole","ABOBEFKCDJB");
    if(!cls)cls=findClass("Assembly-CSharp.dll","","ABOBEFKCDJB");
    if(!cls){current=CurrentContext();if(current)cls=*(void **)current;}
    if(!cls)return false;
    if(!ContextLayout(cls)){Log(@"control_device refused_context_layout=1");installRefused=true;return false;}
    void *targets[4]={};
    for(unsigned i=0;i<4;++i){auto &e=ControlDeviceProfile::entries[i];uintptr_t va=global?e.global:e.cn;
        targets[i]=(void *)RuntimeImageRange(va,96,false);
        if(!targets[i]||memcmp(targets[i],global?e.globalCode:e.cnCode,96)){
            Log(@"control_device refused_code role=%u",i);installRefused=true;return false;
        }
    }
    refreshInfo=MethodAt(cls,(uintptr_t)targets[0],"Void",{});
    selectionInfo=MethodAt(cls,(uintptr_t)targets[1],"Void",{"Int32"});
    keyboardInfo=MethodAt(cls,(uintptr_t)targets[2],"Void",{});
    void *eventInfo=MethodAt(cls,(uintptr_t)targets[3],"Void",{"Int32"});
    if(!refreshInfo||!selectionInfo||!keyboardInfo||!eventInfo){Log(@"control_device refused_method_signature=1");installRefused=true;return false;}
    originalRefresh=(void (*)(void *,void *))NativeCameraPointer::Trampoline(targets[0]);
    originalSelection=(void (*)(void *,int,void *))NativeCameraPointer::Trampoline(targets[1]);
    nativeKeyboard=(void (*)(void *,void *))targets[2];
    originalEventSelection=(void (*)(void *,int,void *))NativeCameraPointer::Trampoline(targets[3]);
    if(!originalRefresh||!originalSelection||!originalEventSelection){Log(@"control_device refused_prologue=1");installRefused=true;return false;}
    void *hookTargets[]={targets[0],targets[1],targets[3]};
    uint8_t original[3][16];for(unsigned i=0;i<3;++i)memcpy(original[i],hookTargets[i],16);
    void *replacements[]={(void *)Refresh,(void *)Selection,(void *)EventSelection};
    for(unsigned i=0;i<3;++i){uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)replacements[i]);
        if(!NativeBridge::PatchCode(hookTargets[i],jump,16)){
            for(unsigned j=0;j<=i;++j)NativeBridge::PatchCode(hookTargets[j],original[j],16);
            Log(@"control_device patch_failed=1");installRefused=true;return false;
        }
    }
    installed=true;bool show=InstallShow();Log(@"control_device installed=1 profile=%s real_native_keyboard_config=1 touch_option_removed=1 native_controller_discovery=1 event_wrapper_guard=1 cached_selector=%d",RuntimeProfileName(),show);
    if(current)Refresh(current,refreshInfo);
    return true;
}
static void Install(void) {
    if(!IsCompatibleRuntime()||!IsModernRuntime())return;
    rowOffset=IsGlobalModernRuntime()?ControlDeviceProfile::GlobalRow:ControlDeviceProfile::CNRow;
    optionsOffset=IsGlobalModernRuntime()?ControlDeviceProfile::GlobalOptions:ControlDeviceProfile::CNOptions;
    InstallShow();
    if(TryInstall()||installRefused)return;
    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *timer){
        if(TryInstall()||installRefused)[timer invalidate];
    }];
}
}
