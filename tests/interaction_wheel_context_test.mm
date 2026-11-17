#import <Foundation/Foundation.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
#include <atomic>
#include <thread>
static double clockValue=10;
static double CACurrentMediaTime(){return clockValue;}
static void Log(NSString *,...){}
static bool IsModernRuntime(){return true;}
static const char *RuntimeProfileName(){return "fixture";}
static const char panelName[]="MonoInteePanel",listName[]="MonoReusableList";
static bool schemaValid=true;
static int finderToken,childToken,interactionTypeToken;
struct alignas(8) Object {char memory[128]{};};
struct Info {bool active=true;std::vector<void *> children;Object countBox;};
static std::map<void *,Info> info;
static Object panel,list1,list2,row1,row2,row3;
static std::vector<uint64_t> panels;
static void *initialize(Object &o,const char *type){*(const char **)o.memory=type;info[&o]={};return &o;}
namespace SceneUIProbe {
static const char *className(void *cls){return (const char *)cls;}
static void *findClass(const char *,const char *,const char *){return (void *)panelName;}
static void *lookup(void *,const char *name,int,int){return !strcmp(name,"GetChild")?&childToken:nullptr;}
static void *Property(void *object,const char *name){
    if(!object)return nullptr;
    if(!strcmp(name,"get_transform")||!strcmp(name,"get_gameObject"))return object;
    if(!strcmp(name,"get_childCount")){auto &n=info[object];*(int *)(n.countBox.memory+16)=(int)n.children.size();return &n.countBox;}
    return nullptr;
}
static bool Bool(void *object,const char *){return object&&info[object].active;}
static void *Call(void *method,void *object=nullptr,void **args=nullptr){
    if(method==&finderToken)return panels.data();
    if(method==&childToken)return info[object].children.at(*(int *)args[0]);
    return nullptr;
}
}
namespace NativeChatFocus {
static bool FieldOffset(void *,const char *field,const char *,size_t &offset){offset=!strcmp(field,"_pickupPanel")?32:40;return schemaValid;}
}
namespace TeleportCursorBridge {
static void *TypeObject(const char *,const char *,const char *){return &interactionTypeToken;}
static void *OneTypeArgument(void *,const char *){return &finderToken;}
static bool Active(void *o){return o&&info[o].active;}
}
namespace DesktopControlPreference {static std::atomic<bool> bootstrapReady{false};}
namespace NativeBridge {static bool (*interactionWheelContext)();static std::atomic<uint64_t> frames{0};}
#include "../genshin/interaction_wheel.h"
static bool check(){clockValue+=.1;return InteractionWheel::Context();}
int main(){@autoreleasepool{
    initialize(panel,panelName);initialize(list1,listName);initialize(list2,listName);
    initialize(row1,"row");initialize(row2,"row");initialize(row3,"row");
    *(void **)(panel.memory+32)=&list1;*(void **)(panel.memory+40)=&list2;
    panels.resize(5);panels[3]=1;panels[4]=(uintptr_t)&panel;
    InteractionWheel::Install();assert(NativeBridge::interactionWheelContext);
    // Constructor/startup cannot touch metadata before the game's input is ready.
    assert(!InteractionWheel::Resolve());DesktopControlPreference::bootstrapReady=true;
    assert(!InteractionWheel::Resolve());NativeBridge::frames=101;assert(InteractionWheel::Resolve());
    assert(!check());info[&list1].children={&row1};assert(!check());
    info[&list1].children={&row1,&row2};assert(check());
    // Catalyst drives native input on an engine thread distinct from AppKit.
    // Requiring NSThread.isMainThread would silently leave smoothing enabled.
    std::thread engine([]{ @autoreleasepool{assert(!NSThread.isMainThread);assert(check());} });engine.join();
    info[&row2].active=false;assert(!check());
    // One pickup and one dialog are also a two-choice F list.
    info[&list2].children={&row3};assert(check());
    info[&list2].active=false;assert(!check());info[&list2].active=true;
    info[&panel].active=false;assert(!check());info[&panel].active=true;
    // Object pools, unrelated list types and invalid sizes do not activate it.
    *(const char **)list1.memory="OtherList";assert(!check());
    *(const char **)list1.memory=listName;info[&list1].children.resize(129,&row1);assert(!check());
    panels[3]=9;assert(!check());panels[3]=1;
    schemaValid=false;InteractionWheel::ready=false;assert(!check());
    puts("PASS actual interaction context: pickups, mixed dialog rows, inactive pools/pages, wrong types, bounds, missing schema");
}}
