#pragma once
#include "native_team_profiles.h"
#include "mouse_drag_state.h"
// Mouse capture for the native Ley Line team reorder context. The game still
// picks the source/destination and applies all eligibility and exchange rules.
// No Touch, Gesture, team-array write, or device-mode spoof is involved.
namespace NativeTeamPointer {
using namespace SceneUIProbe;
struct V2 {float x,y;};
static const NativeTeamProfiles::Profile *profile;
static void (*originalUpdate)(void *,void *),(*originalDestroy)(void *,void *);
static V2 (*originalPosition)(void *,void *);
static void *pickMethod,*beginMethod,*endMethod,*cancelMethod;
static void *findInstance,*isChildOf,*boundClass;
static std::vector<void *> refusedClasses;
static bool installed;
static void *captured;
static int rootID,parentID,sourceID;
static uint64_t captureRevision;
static V2 position;
static double pointScaleX,pointScaleY;
static int renderWidth,renderHeight;
static DesktopMouseDragState drag;

static void Reset(){captured=nullptr;rootID=parentID=sourceID=0;drag.cancel();++captureRevision;}
static bool Active(void *object){return object&&Bool(Property(object,"get_gameObject"),"get_activeInHierarchy");}
static void *Find(int id){void *args[]={&id};return id?Call(findInstance,nullptr,args):nullptr;}
static bool Within(void *hitTransform,void *list){
 void *root=list?Property(list,"get_transform"):nullptr;if(!root||!hitTransform||!Active(list))return false;
 void *args[]={root},*v=Call(isChildOf,hitTransform,args);return v&&*(bool *)((char *)v+16);
}
static void *Pointer(){
 char *owner=NativePC::InputOwner();void *module=owner?*(void **)(owner+GenshinOffsets().ownerModule):nullptr;
 return module?NativePC::MousePointer(module):nullptr;
}
#ifdef GDM_TEAM_TRACE
static void TraceRoot(void *object,void *pointer){
 void *root=*(void **)((char *)object+profile->root);
 Log(@"native_team_trace context=%p root=%@ active=%d pointer=%p",object,Name(root),Active(root),pointer);
 void *hit=Property(pointer,"get_pointerEnter"),*transform=Property(hit,"get_transform");
 for(unsigned depth=0;transform&&depth<12;++depth){
  Log(@"native_team_trace hit_parent=%u name=%@",depth,Name(transform));
  transform=Property(transform,"get_parent");
 }
}
#endif
static bool Scope(void *object,void *pointer){
 void *hit=Property(pointer,"get_pointerEnter"),*transform=Property(hit,"get_transform");if(!transform)return false;
 // The same typed native reorder context is shared by the event main page
 // and LLCLevelPageContext. Scope against its own Init root, not one view.
 void *root=*(void **)((char *)object+profile->root);
 if(!Within(transform,root))return false;
 rootID=NativeChatFocus::ID(root);return rootID!=0;
}
static bool NativeDragging(void *object){return *(bool *)((char *)object+profile->dragging);}
static V2 Position(void *object,void *method){return captured==object?position:originalPosition(object,method);}
static void Finish(void *object,bool commit){
 if(NativeDragging(object)){
  // Position remains owned until the native end handler completes its pick.
  bool ok=ControlDevice::VoidCall(commit?endMethod:cancelMethod,object);
  Log(@"native_team_mouse drag=0 commit=%d delivered=%d virtual_touch=0",commit,ok);
 }
 Reset();
}
static void SuppressDragClick(){
 void *pointer=Pointer();bool no=false;void *args[]={&no};
 if(pointer)Call(lookup(*(void **)pointer,"set_eligibleForClick",1,0),pointer,args);
}
static void MouseFrame(void *object){
 if(captured&&object!=captured)return;
 bool held=(NativeBridge::physicalMouseSnapshot.load()&1)!=0;
 uint64_t focus=NativeBridge::mapFocusRevision.load();
 if(!held&&!captured){drag.sample(false,false,false,focus,0,0);return;}
 bool enabled=NativeBridge::nativeDesktopMouseUI.load()&&!NativeBridge::controllerInputActive.load()&&
  !NativeBridge::nativeOverlayActive.load()&&DesktopControlPreference::gameplayReady.load()&&
  !pointerCaptureSuspended&&WantsVisibleCursor()&&NativeBridge::IsForeground();
 V2 p{nativeInputManager?*(float *)(nativeInputManager+0xd0):NAN,nativeInputManager?*(float *)(nativeInputManager+0xd4):NAN};
 const auto [w,h]=NativeBridge::screenMetrics.read();
 bool inside=w>0&&h>0&&std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=0&&p.y>=0&&p.x<w&&p.y<h;
 bool fresh=held&&!drag.held,mayBegin=false;
#ifdef GDM_TEAM_TRACE
 static unsigned tracePresses=0;
 if(fresh&&tracePresses++<24){
  Log(@"native_team_trace press=%u enabled=%d inside=%d native_drag=%d pos=(%.1f,%.1f) size=%dx%d cursor=%d foreground=%d overlay=%d ready=%d ui=%d controller=%d suspended=%d",tracePresses,enabled,inside,NativeDragging(object),p.x,p.y,w,h,WantsVisibleCursor(),NativeBridge::IsForeground(),NativeBridge::nativeOverlayActive.load(),DesktopControlPreference::gameplayReady.load(),NativeBridge::nativeDesktopMouseUI.load(),NativeBridge::controllerInputActive.load(),pointerCaptureSuspended);
  TraceRoot(object,Pointer());
 }
#endif
 if(fresh&&!captured&&enabled&&inside&&!NativeDragging(object)&&Scope(object,Pointer())){
  uint64_t uid=0;uint32_t slot=0;void *source=nullptr;void *args[]={&p,&uid,&slot,&source};
  void *picked=Call(pickMethod,object,args);CGRect bounds{};
  void *hit=Property(Pointer(),"get_pointerEnter"),*transform=Property(hit,"get_transform");
  // Native hit testing can see a card behind a popup. Require the current
  // UI raycast to be that exact source or its child before taking capture.
  mayBegin=picked&&*(bool *)((char *)picked+16)&&source&&Within(transform,source)&&
   GameContentBounds(bounds)&&bounds.size.width>0&&bounds.size.height>0;
#ifdef GDM_TEAM_TRACE
  Log(@"native_team_trace scope=1 picked=%d source=%p bounds=(%.1f,%.1f) may_begin=%d",picked&&*(bool *)((char *)picked+16),source,bounds.size.width,bounds.size.height,mayBegin);
#endif
  if(mayBegin){
   sourceID=NativeChatFocus::ID(source);parentID=NativeChatFocus::ID(Property(source,"get_parent"));mayBegin=sourceID&&parentID;
   if(mayBegin){captured=object;position=p;renderWidth=w;renderHeight=h;pointScaleX=bounds.size.width/w;pointScaleY=bounds.size.height/h;
    Log(@"native_team_mouse captured=1 native_pick=1 slot=%u virtual_touch=0",slot);}
  }
 }
 bool available=enabled&&inside&&captured==object&&w==renderWidth&&h==renderHeight;
 if(available){void *root=Find(rootID),*parent=Find(parentID);available=root&&parent&&Active(root)&&Active(parent);}
 bool sameFocus=focus==drag.revision;
 if(captured==object&&inside)position=p;
 auto step=drag.sample(held,available,mayBegin,focus,(float)(p.x*pointScaleX),(float)(p.y*pointScaleY),5);
 if(step.ended){Finish(object,!held&&available&&sameFocus);return;}
 if(step.began){
  void *source=Find(sourceID);int mouse=-1;void *args[]={source,&mouse};uint64_t rev=captureRevision;
  bool ok=source&&Active(source)&&ControlDevice::VoidCall(beginMethod,object,args);
  if(captured!=object||captureRevision!=rev)return; // Native callbacks may rebuild/close the page.
  bool began=ok&&NativeDragging(object);
  Log(@"native_team_mouse drag=%d native_begin=%d virtual_touch=0",began,ok);
  if(!began){Reset();return;}SuppressDragClick();
 }
 if(drag.dragging&&!NativeDragging(object)){Reset();return;}
 if(captured==object&&!drag.armed&&!drag.dragging)Reset();
}
// Resolve by code address and managed signature, allowing CN/Global's shuffled
// names and layouts while refusing unknown entries. No per-frame scene scan.
static void *TypedBody(void *cls,const NativeTeamProfiles::Entry &entry,const char *result,
                       std::initializer_list<const char *> parameters){
 auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5f8);
 auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);
 auto count=EngineFunction<unsigned (*)(void *)>(0x10e9bb700);
 auto param=EngineFunction<void *(*)(void *,unsigned)>(0x10e9bb704);
 auto ret=EngineFunction<void *(*)(void *)>(0x10e9bb6e8);
 auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
 const void *body=RuntimeImageRange(entry.address,32,false);
 for(unsigned depth=0;cls&&depth<16;cls=parent(cls),++depth){void *it=nullptr;
  while(void *m=next(cls,&it)){
   if(ManagedMethodCode(m)!=(uintptr_t)body||count(m)!=parameters.size())continue;
   void *r=from(ret(m));if(!r||strcmp(className(r),result))return nullptr;
   unsigned i=0;for(const char *type:parameters){void *pc=from(param(m,i++));if(!pc||strcmp(className(pc),type))return nullptr;}
   return m;
  }
 }return nullptr;
}
static bool Bind(void *object){
 void *cls=*(void **)object;if(cls==boundClass)return true;
 for(void *refused:refusedClasses)if(cls==refused)return false;
 bool recognized=TypedBody(cls,profile->teamInit,"Void",{"GameObject"})&&
  TypedBody(cls,profile->exchange,"Void",{"Boolean","UInt64","UInt32","UInt64","UInt32"});
 if(recognized){
  pickMethod=TypedBody(cls,profile->pick,"Boolean",{"Vector2","UInt64","UInt32","Transform"});
  beginMethod=TypedBody(cls,profile->begin,"Void",{"Transform","Int32"});
  endMethod=TypedBody(cls,profile->end,"Void",{});cancelMethod=TypedBody(cls,profile->cancel,"Void",{});
  if(pickMethod&&beginMethod&&endMethod&&cancelMethod&&
     TypedBody(cls,profile->position,"Vector2",{})&&HasManagedField(cls,nullptr,profile->root,"Transform")&&
     HasManagedField(cls,nullptr,profile->dragging,"Boolean")){
   boundClass=cls;drag.held=(NativeBridge::physicalMouseSnapshot.load()&1)!=0;
   drag.revision=NativeBridge::mapFocusRevision.load();
   Log(@"native_team_mouse bound=1 class=%s native_reorder=1 root_offset=%zu drag_offset=%zu profile=%s",className(cls),profile->root,profile->dragging,RuntimeProfileName());return true;
  }
  Log(@"native_team_mouse refused_metadata=1 class=%s",className(cls));
 }
 if(refusedClasses.size()<128)refusedClasses.push_back(cls);return false;
}
static void Update(void *object,void *method){if(Bind(object))MouseFrame(object);originalUpdate(object,method);}
static void Destroy(void *object,void *method){if(captured==object)Finish(object,false);originalDestroy(object,method);}
static bool TryInstall(){
 if(installed)return true;if(!IsModernRuntime()||!NativeBridge::frames.load()||!DesktopControlPreference::bootstrapReady.load())return false;
 profile=IsGlobalModernRuntime()?&NativeTeamProfiles::Global:&NativeTeamProfiles::CN;
 const NativeTeamProfiles::Entry *entries[]={&profile->update,&profile->destroy,&profile->position,&profile->pick,&profile->begin,&profile->end,&profile->cancel,&profile->teamInit,&profile->exchange};
 for(auto e:entries){const void *p=RuntimeImageRange(e->address,32,false);if(!p||memcmp(p,e->expected,32))return false;}
 findInstance=ControlDevice::Method(findClass("UnityEngine.CoreModule.dll","UnityEngine","Object"),"FindObjectFromInstanceID","Object",{"Int32"});
 isChildOf=ControlDevice::Method(findClass("UnityEngine.CoreModule.dll","UnityEngine","Transform"),"IsChildOf","Boolean",{"Transform"});
 if(!findInstance||!isChildOf)return false;
 void *targets[3],*trampolines[3]{};uint8_t saved[3][16];
 for(unsigned i=0;i<3;++i){targets[i]=(void *)RuntimeImageRange(entries[i]->address,32,false);
  if(!NativeCameraPointer::RegisterOnlyPrologue(targets[i]))return false;memcpy(saved[i],targets[i],16);}
 for(unsigned i=0;i<3;++i)if(!(trampolines[i]=NativeCameraPointer::Trampoline(targets[i]))){
  for(unsigned j=0;j<i;++j)munmap(trampolines[j],4096);return false;}
 originalUpdate=(decltype(originalUpdate))trampolines[0];originalDestroy=(decltype(originalDestroy))trampolines[1];originalPosition=(decltype(originalPosition))trampolines[2];
 void *replacements[]={(void *)Update,(void *)Destroy,(void *)Position};
 for(unsigned i=0;i<3;++i){uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)replacements[i]);
  if(!NativeBridge::PatchCode(targets[i],jump,16)){
   for(unsigned j=0;j<=i;++j)NativeBridge::PatchCode(targets[j],saved[j],16);return false;}}
 installed=true;Log(@"native_team_mouse installed=1 source=absolute_mouse native_begin_end_pick=1 virtual_touch=0 profile=%s",RuntimeProfileName());return true;
}
static void Install(){
 if(TryInstall())return;__block unsigned attempts=0;
 [NSTimer scheduledTimerWithTimeInterval:.5 repeats:YES block:^(NSTimer *timer){if(TryInstall()||++attempts>=120)[timer invalidate];}];
}
}
