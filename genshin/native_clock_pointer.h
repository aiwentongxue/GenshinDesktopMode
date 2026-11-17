#pragma once
#include "native_clock_profiles.h"
// Deliver the existing UGUI mouse drag to the clock's direction-based input.
// The native page retains time limits, day crossings, sound and release logic.
namespace NativeClockPointer {
using namespace SceneUIProbe;
struct V2 {float x,y;};struct V3 {float x,y,z;};
static void (*originalDrag)(void *,void *,void *);
static void *pageType,*parentMethod,*setTarget,*worldToScreen;
static size_t handOffset,rootOffset,usableOffset,holdOffset;
static bool installed;
static bool Profile(void *m,const NativeClockProfiles::Entry &entry){
 const void *body=RuntimeImageRange(entry.address,sizeof(entry.expected),false);
 return m&&body&&ManagedMethodCode(m)==(uintptr_t)body&&!memcmp(body,entry.expected,sizeof(entry.expected));
}
static bool MouseEvent(void *event){
 if(!event||!NativeBridge::nativeDesktopMouseUI.load()||NativeBridge::controllerInputActive||NativeBridge::nativeOverlayActive||
    !NativeBridge::IsForeground()||pointerCaptureSuspended||!(NativeBridge::physicalMouseSnapshot.load()&1))return false;
 void *id=Property(event,"get_pointerId"),*button=Property(event,"get_button");
 return id&&*(int *)((char *)id+16)==-1&&button&&*(int *)((char *)button+16)==0;
}
static void Drag(void *button,void *event,void *method){
 originalDrag(button,event,method);
 if(!MouseEvent(event))return;
 void *args[]={pageType},*page=Call(parentMethod,button,args);
 if(!page||strcmp(className(*(void **)page),"MonoInLevelClockPage")||!Bool(page,"get_isActiveAndEnabled")||
    !*(bool *)((char *)page+usableOffset)||*(void **)((char *)page+handOffset)!=button||
    !*(bool *)((char *)button+holdOffset))return;
 void *root=*(void **)((char *)page+rootOffset),*position=Property(event,"get_position"),*world=root?Property(root,"get_position"):nullptr;
 if(!root||!position||!world)return;
 V2 p;V3 w;memcpy(&p,(char *)position+16,sizeof(p));memcpy(&w,(char *)world+16,sizeof(w));
 void *camera=Property(event,"get_pressEventCamera"),*projectArgs[]={camera,&w},*center=Call(worldToScreen,nullptr,projectArgs);
 if(!center)return;V2 c;memcpy(&c,(char *)center+16,sizeof(c));V2 direction{p.x-c.x,p.y-c.y};
 if(!std::isfinite(direction.x)||!std::isfinite(direction.y)||std::hypot(direction.x,direction.y)<1)return;
 void *nativeArgs[]={&direction};bool ok=ControlDevice::VoidCall(setTarget,page,nativeArgs);
 static unsigned packets;if(++packets<=3||packets%240==0)Log(@"native_clock_mouse packets=%u delivered=%d virtual_touch=0 original_time_rules=1",packets,ok);
}
static bool TryInstall(){
 if(installed)return true;
 if(!NativeBridge::frames.load()||!DesktopControlPreference::bootstrapReady.load())return false;
 void *page=findClass("Assembly-CSharp.dll","MoleMole","MonoInLevelClockPage"),*button=findClass("Assembly-CSharp.dll","MoleMole","MonoActionBtn");
 if(!page||!button)return false;
 void *drag=ControlDevice::Method(button,"OnDrag","Void",{"PointerEventData"});
 setTarget=ControlDevice::Method(page,"SetTargetTime","Void",{"Vector2"});
 bool global=IsGlobalModernRuntime();
 if(!Profile(drag,global?NativeClockProfiles::button_drag_global:NativeClockProfiles::button_drag_cn)||
    !Profile(setTarget,global?NativeClockProfiles::clock_set_target_global:NativeClockProfiles::clock_set_target_cn))return false;
 if(!NativeChatFocus::FieldOffset(page,"_hourHand","MonoActionBtn",handOffset)||
    !NativeChatFocus::FieldOffset(page,"_hourHandRoot","Transform",rootOffset)||
    !NativeChatFocus::FieldOffset(page,"canUseClock","Boolean",usableOffset)||
    !NativeChatFocus::FieldOffset(button,"_isPointerHold","Boolean",holdOffset))return false;
 pageType=TeleportCursorBridge::TypeObject("Assembly-CSharp.dll","MoleMole","MonoInLevelClockPage");
 parentMethod=TeleportCursorBridge::OneTypeArgument(findClass("UnityEngine.CoreModule.dll","UnityEngine","Component"),"GetComponentInParent");
 void *utility=findClass("UnityEngine.UIModule.dll","UnityEngine","RectTransformUtility");
 worldToScreen=ControlDevice::Method(utility,"WorldToScreenPoint","Vector2",{"Camera","Vector3"});
 if(!pageType||!parentMethod||!worldToScreen)return false;
 void *target=(void *)ManagedMethodCode(drag);if(!NativeCameraPointer::RegisterOnlyPrologue(target))return false;
 originalDrag=(decltype(originalDrag))NativeCameraPointer::Trampoline(target);if(!originalDrag)return false;
 uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)Drag);
 if(!NativeBridge::PatchCode(target,jump,16))return false;
 installed=true;Log(@"native_clock_mouse installed=1 native_pointer_event=1 original_time_rules=1 virtual_touch=0 profile=%s",RuntimeProfileName());return true;
}
static void Install(){
 if(TryInstall())return;
 static bool waiting;if(waiting)return;waiting=true;
 __block unsigned attempts=0;
 [NSTimer scheduledTimerWithTimeInterval:.5 repeats:YES block:^(NSTimer *timer){
  if(TryInstall()||++attempts>=120){waiting=false;[timer invalidate];}
 }];
}
}
