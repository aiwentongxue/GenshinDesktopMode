#pragma once
#include <atomic>
#include <mutex>
#include "controller_transport.h"

namespace DesktopControllerIdentity {
using namespace DesktopControllerUI;
static std::mutex mutex;
static GCController *selected;
static Model selectedModel=Model::Unknown;
static std::atomic<bool> playStation{false},hasPlayStation{false};
static std::atomic<Skin> skin{Skin::Native};
static std::atomic<uint64_t> revision{1};
static void (*changed)();
static DesktopControllerTransport::Snapshot endpoints;

static bool ProfileIs(GCController *controller,NSString *name) {
    Class cls=NSClassFromString(name);return cls&&
        ([controller.extendedGamepad isKindOfClass:cls]||[controller.physicalInputProfile isKindOfClass:cls]);
}
static Model Identify(GCController *controller) {
    if(!controller||controller.isSnapshot||!controller.extendedGamepad)return Model::Unknown;
    if(ProfileIs(controller,@"GCDualSenseGamepad"))return Model::DualSense;
    if(ProfileIs(controller,@"GCDualShockGamepad"))return Model::DualShock;
    if(ProfileIs(controller,@"GCXboxGamepad"))return Model::Xbox;
    for(NSString *label in @[controller.productCategory?:@"",controller.vendorName?:@""]){
        NSString *name=label.lowercaseString;
        if([name containsString:@"dualsense"])return Model::DualSense;
        if([name containsString:@"dualshock"])return Model::DualShock;
        if([name containsString:@"playstation"])return Model::PlayStation;
        if([name containsString:@"xbox"])return Model::Xbox;
    }
    return Model::Unknown;
}
static bool IsPS(Model model) {
    return model==Model::DualSense||model==Model::DualShock||model==Model::PlayStation;
}
static bool IsPlayStation(GCController *controller) {return IsPS(Identify(controller));}
static bool Connected(GCController *controller) {
    return controller&&!controller.isSnapshot&&controller.extendedGamepad&&
        [GCController.controllers containsObject:controller];
}
// Caller holds mutex. Matching by family is deliberately conservative when
// multiple same-model pads are connected; a random USB pad must not relabel BT.
static Skin ResolveSkin(Model model) {
    unsigned matching=0;
    for(GCController *c in GCController.controllers)if(Identify(c)==model)++matching;
    Transport transport=Transport::Unknown;
    if(model==Model::DualSense)transport=ResolveTransport(matching,endpoints.dualSense);
    else if(model==Model::DualShock)transport=ResolveTransport(matching,endpoints.dualShock);
    return Resolve(model,transport);
}
static void Select(GCController *controller,bool isPS) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(selected!=controller||playStation.load()!=isPS){
            selected=controller;selectedModel=Identify(controller);
            playStation=IsPS(selectedModel);skin=ResolveSkin(selectedModel);++revision;
        }
    }
    // No scene work on the input thread; the callback is coalesced.
    if(changed)changed();
}
static void Refresh() {
    NSArray<GCController *> *controllers=GCController.controllers;
    {
        std::lock_guard<std::mutex> lock(mutex);
        GCController *retained=nil,*only=nil;unsigned count=0;bool anyPS=false;
        for(GCController *c in controllers)if(c.extendedGamepad&&!c.isSnapshot){
            ++count;only=c;if(c==selected)retained=c;anyPS|=IsPlayStation(c);
        }
        hasPlayStation=anyPS;
        GCController *next=retained?:count==1?only:nil;
        Model model=Identify(next);Skin resolved=ResolveSkin(model);
        if(selected!=next||selectedModel!=model||skin.load()!=resolved){
            selected=next;selectedModel=model;playStation=IsPS(model);skin=resolved;++revision;
        }
    }
    if(changed)changed();
}
static void InstallTransportMonitor() {
    static dispatch_source_t timer;if(timer)return;
    dispatch_queue_t queue=dispatch_queue_create("local.genshin.controller-ui-metadata",DISPATCH_QUEUE_SERIAL);
    timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,queue);
    dispatch_source_set_timer(timer,DISPATCH_TIME_NOW,NSEC_PER_SEC,NSEC_PER_SEC/4);
    dispatch_source_set_event_handler(timer,^{@autoreleasepool{
        if(!hasPlayStation.load())return;
        auto now=DesktopControllerTransport::Read();bool different=false;
        {std::lock_guard<std::mutex> lock(mutex);different=!(now==endpoints);if(different)endpoints=now;}
        if(different)dispatch_async(dispatch_get_main_queue(),^{Refresh();});
    }});
    dispatch_resume(timer);
}
static GCControllerButtonInput *ChatButton(GCController *controller,bool isPS) {
    // Apple buttonA is PS cross. Other pads retain the left-face shortcut.
    return isPS?controller.extendedGamepad.buttonA:controller.extendedGamepad.buttonX;
}
}
