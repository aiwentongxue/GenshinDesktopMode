#pragma once
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <cmath>
#include <array>

class DesktopMouseEventSequence {
    struct Key {double time=0;NSInteger type=0,number=0,window=0;uintptr_t receiver=0;};
    std::array<Key,32> recent{};
    unsigned next=0;
public:
    void reset() {recent={};next=0;}
    bool firstDelivery(id event,id receiver=nil) {
        auto integer=[](id object,const char *name) {
            return ((NSInteger (*)(id,SEL))objc_msgSend)(object,sel_registerName(name));
        };
        Key key={((double (*)(id,SEL))objc_msgSend)(event,sel_registerName("timestamp")),
            integer(event,"type"),integer(event,"eventNumber"),integer(event,"windowNumber"),
            (uintptr_t)(__bridge void *)receiver};
        for(auto old:recent)
            if(old.time==key.time && old.type==key.type && old.number==key.number && old.window==key.window && old.receiver==key.receiver) return false;
        recent[next++%recent.size()]=key;return true;
    }
};

// Replace only an existing app-local mouse event. Nothing is posted to the
// system, and the hardware cursor does not move. UIKit must receive the same
// location as Unity and the visible cursor while the physical mouse is held.
static id DesktopMouseEventAtPosition(id event,CGPoint screenPoint,CGPoint windowPoint,
                                     CGPoint delta,int &conversion) {
    conversion=0;
    Class eventClass=NSClassFromString(@"NSEvent");
    auto integer=[](id object,const char *name) {
        return ((NSInteger (*)(id,SEL))objc_msgSend)(object,sel_registerName(name));
    };
    NSInteger type=integer(event,"type"),window=integer(event,"windowNumber");
    NSInteger button=integer(event,"buttonNumber");
    auto matches=[&](id candidate) {
        if(!candidate || integer(candidate,"type")!=type ||
           integer(candidate,"windowNumber")!=window || integer(candidate,"buttonNumber")!=button) return false;
        CGPoint actual=((CGPoint (*)(id,SEL))objc_msgSend)(candidate,sel_registerName("locationInWindow"));
        return fabs(actual.x-windowPoint.x)<0.01 && fabs(actual.y-windowPoint.y)<0.01;
    };
    // A copied CGEvent preserves pressure, button identity and event metadata.
    // AppKit versions differ in how they retain window-local coordinates, so
    // verify the converted location instead of assuming SetLocation updated it.
    static auto copyEvent=(CFTypeRef (*)(CFTypeRef))dlsym(RTLD_DEFAULT,"CGEventCreateCopy");
    static auto setLocation=(void (*)(CFTypeRef,CGPoint))dlsym(RTLD_DEFAULT,"CGEventSetLocation");
    static auto setDouble=(void (*)(CFTypeRef,uint32_t,double))dlsym(RTLD_DEFAULT,"CGEventSetDoubleValueField");
    if(copyEvent && setLocation && setDouble) {
        CFTypeRef source=((CFTypeRef (*)(id,SEL))objc_msgSend)(event,sel_registerName("CGEvent"));
        CFTypeRef copy=source?copyEvent(source):nullptr;
        if(copy) {
            setLocation(copy,screenPoint);
            setDouble(copy,4,delta.x);setDouble(copy,5,delta.y); // kCGMouseEventDeltaX/Y
            id replacement=((id (*)(id,SEL,CFTypeRef))objc_msgSend)(eventClass,sel_registerName("eventWithCGEvent:"),copy);
            CFRelease(copy);
            if(matches(replacement)) {conversion=1;return replacement;}
        }
    }
    // Explicit window-local construction is the public fallback. Dragging in
    // UIKit uses these positions; native camera axes were already forwarded.
    NSUInteger flags=((NSUInteger (*)(id,SEL))objc_msgSend)(event,sel_registerName("modifierFlags"));
    double time=((double (*)(id,SEL))objc_msgSend)(event,sel_registerName("timestamp"));
    float pressure=((float (*)(id,SEL))objc_msgSend)(event,sel_registerName("pressure"));
    id replacement=((id (*)(id,SEL,NSUInteger,CGPoint,NSUInteger,double,NSInteger,id,NSInteger,NSInteger,float))objc_msgSend)(
        eventClass,sel_registerName("mouseEventWithType:location:modifierFlags:timestamp:windowNumber:context:eventNumber:clickCount:pressure:"),
        type,windowPoint,flags,time,window,nil,integer(event,"eventNumber"),integer(event,"clickCount"),pressure);
    if(matches(replacement)) {conversion=2;return replacement;}
    return event;
}
