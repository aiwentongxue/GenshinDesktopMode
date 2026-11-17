#pragma once
#include "chat_focus_request.h"
// Activate the existing chat edit box. No key synthesis, click simulation,
// text replacement, or submit callback is used by this bridge.
namespace NativeChatFocus {
using namespace SceneUIProbe;
static std::atomic<uint64_t> revision{0};
static DesktopChatFocusRequest request;
static GCController *requestController;
static void *pageType,*finder;
static bool FieldOffset(void *cls,const char *name,const char *type,size_t &result){
 auto next=EngineFunction<void *(*)(void *,void **)>(0x10e9bb5ec);auto fn=EngineFunction<const char *(*)(void *)>(0x10e9bb690);
 auto flags=EngineFunction<unsigned (*)(void *)>(0x10e9bb694);auto at=EngineFunction<unsigned (*)(void *)>(0x10e9bb69c);auto parent=EngineFunction<void *(*)(void *)>(0x10e9bb608);auto from=EngineFunction<void *(*)(void *)>(0x10e9bb5e0);
 for(unsigned i=0;cls&&i<16;cls=parent(cls),++i){void *it=nullptr;while(void *f=next(cls,&it))if(!(flags(f)&0x10)&&!strcmp(fn(f),name)){
  void *c=from(ManagedFieldType(f));unsigned offset=at(f);if(c&&!strcmp(className(c),type)&&offset>=16&&offset<8192){result=offset;return true;}}}return false;
}
static int ID(void *o){void *v=Property(o,"GetInstanceID");return v?*(int *)((char *)v+16):0;}
static void *Page(){
 if(!DesktopControlPreference::bootstrapReady.load()||!DesktopControlPreference::gameplayReady.load())return nullptr;
 if(!pageType)pageType=TeleportCursorBridge::TypeObject("Assembly-CSharp.dll","MoleMole","MonoInLevelChatPage");
 if(!finder)finder=TeleportCursorBridge::OneTypeArgument(findClass("UnityEngine.CoreModule.dll","UnityEngine","Resources"),"FindObjectsOfTypeAll");
 void *args[]={pageType},*array=pageType&&finder?Call(finder,nullptr,args):nullptr;uint64_t count=array?*(uint64_t *)((char *)array+24):0;
 if(count>32)return nullptr;void *matched=nullptr;
 for(uint64_t i=0;i<count;++i){void *p=((void **)((char *)array+32))[i];if(!p||strcmp(className(*(void **)p),"MonoInLevelChatPage")||!Bool(Property(p,"get_gameObject"),"get_activeInHierarchy"))continue;
  if(matched)return nullptr;matched=p;}return matched;
}
static bool TextResponder(UIView *view,unsigned &budget){
 if(!view||!budget)return false;--budget;if(view.hidden||view.alpha<.02)return false;
 if(([view isKindOfClass:UITextField.class]||[view isKindOfClass:UITextView.class])&&view.isFirstResponder)return true;
 for(UIView *child in view.subviews)if(TextResponder(child,budget))return true;return false;
}
static bool Editing(void *page){
 for(UIWindow *window in UIApplication.sharedApplication.windows)if(!window.hidden&&window.alpha>=.02){unsigned budget=512;if(TextResponder(window,budget))return true;}
 size_t offset=0;if(!page||!FieldOffset(*(void **)page,"talkInputField","MonoInputFieldExtention",offset))return false;
 void *field=*(void **)((char *)page+offset);return field&&Bool(field,"get_isFocused");
}
static bool Activate(void *page){
 if(Editing(page))return true;size_t offset=0;void *box=nullptr,*field=nullptr;
 if(FieldOffset(*(void **)page,"talkInputFieldNative","NativeEditBox",offset))box=*(void **)((char *)page+offset);
 if(FieldOffset(*(void **)page,"talkInputField","MonoInputFieldExtention",offset))field=*(void **)((char *)page+offset);
 if(box&&!strcmp(className(*(void **)box),"NativeEditBox")&&Bool(box,"get_isActiveAndEnabled")){
  void *visible=ControlDevice::Method(*(void **)box,"SetVisible","Void",{"Boolean"});
  void *focus=ControlDevice::Method(*(void **)box,"SetFocus","Void",{"Boolean"});bool yes=true;void *args[]={&yes};
  if(visible&&focus&&ControlDevice::VoidCall(visible,box,args)&&ControlDevice::VoidCall(focus,box,args)){
   return true;}
 }
 if(field&&!strcmp(className(*(void **)field),"MonoInputFieldExtention")&&Bool(field,"get_isActiveAndEnabled")){
  void *m=ControlDevice::Method(*(void **)field,"ActivateInputFieldEx","Void",{});
  if(m&&ControlDevice::VoidCall(m,field))return true;
 }return false;
}
static void Attempt(uint64_t expected,unsigned attempt){
 if(expected!=revision.load())return;
 if(!NativeBridge::IsForeground()||NativeBridge::nativeOverlayActive||
    (requestController&&!DesktopControllerIdentity::Connected(requestController))){requestController=nil;return;}
 void *page=Page();char *owner=VerifiedInputOwner();int mode=owner?*(int *)(owner+GenshinOffsets().ownerInputMode):0;
 auto step=request.observe(page?ID(page):0,mode,DesktopUI::layoutSwitchDepth!=0,page&&Editing(page),CACurrentMediaTime());
 if(step==DesktopChatFocusRequest::Step::Complete){
  Log(@"native_chat_focus verified=1 mode=%d attempts=%u main_thread=1 text_untouched=1 submit_called=0",mode,request.activations);requestController=nil;return;
 }
 if(step==DesktopChatFocusRequest::Step::Cancel||attempt>=22){requestController=nil;return;}
 if(step==DesktopChatFocusRequest::Step::Activate)Activate(page);
 dispatch_after(dispatch_time(DISPATCH_TIME_NOW,50*NSEC_PER_MSEC),dispatch_get_main_queue(),^{Attempt(expected,attempt+1);});
}
static bool Request(int mode,GCController *controller=nil){
 if(!NSThread.isMainThread)return false;
 if(!NativeBridge::IsForeground()||NativeBridge::nativeOverlayActive)return false;void *page=Page();if(!page||Editing(page))return false;
 int id=ID(page);if(!id)return false;
 char *owner=VerifiedInputOwner();int current=owner?*(int *)(owner+GenshinOffsets().ownerInputMode):mode;
 request.begin(id,current,mode,CACurrentMediaTime());requestController=controller;
 uint64_t expected=++revision;dispatch_async(dispatch_get_main_queue(),^{Attempt(expected,0);});return true;
}
static void Keyboard(id event){
 NSUInteger type=((NSUInteger (*)(id,SEL))objc_msgSend)(event,sel_registerName("type"));
 if(type==1||type==3||type==25){++revision;return;}if(type!=10)return;
 unsigned short key=((unsigned short (*)(id,SEL))objc_msgSend)(event,sel_registerName("keyCode"));
 if(key!=36&&key!=76){++revision;return;}
 NSUInteger flags=((NSUInteger (*)(id,SEL))objc_msgSend)(event,sel_registerName("modifierFlags"));
 if((flags&((1ULL<<17)|(1ULL<<18)|(1ULL<<19)|(1ULL<<20)))||((BOOL (*)(id,SEL))objc_msgSend)(event,sel_registerName("isARepeat")))return;
 if(Request(2)){std::lock_guard<std::mutex> guard(NativeBridge::mutex);NativeBridge::keyboard.blockUntilRelease(13);}
}
static void Controller(GCController *controller){
 // Input sampling is not the UIKit/Unity main thread. Do not inspect pages or
 // responders until this request is dispatched, and discard stale buttons.
 uint64_t expected=revision.load();double deadline=CACurrentMediaTime()+.25;
 dispatch_async(dispatch_get_main_queue(),^{
  if(expected!=revision.load()||CACurrentMediaTime()>deadline||!DesktopControllerIdentity::Connected(controller))return;
  Request(3,controller);
 });
}
static void Install(){
 DesktopControlHotSwap::chatEvent=Keyboard;DesktopControlHotSwap::chatControllerConfirm=Controller;
 for(NSString *name in @[UIApplicationWillResignActiveNotification,@"NSApplicationWillResignActiveNotification"])
  [NSNotificationCenter.defaultCenter addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *){++revision;requestController=nil;}];
 Log(@"native_chat_focus installed=1 keyboard_enter=1 ps_cross=1 other_left_face=1 main_queue=1 focus_verified=1 original_edit_box=1 text_untouched=1 submit_interception=0");
}
}
