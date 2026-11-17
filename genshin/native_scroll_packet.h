#pragma once
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdint>

// In-process copies only. No CGEventPost, global event tap, cursor operation,
// or conversion between line/pixel units is used for a browser wheel notch.
namespace DesktopNativeScrollPacket {
static constexpr uint64_t replayMarker=0x47444d5343524c31ULL;
static auto copy=(CFTypeRef (*)(CFTypeRef))dlsym(RTLD_DEFAULT,"CGEventCreateCopy");
static auto scalar=(double (*)(CFTypeRef,uint32_t))dlsym(RTLD_DEFAULT,"CGEventGetDoubleValueField");
static auto integer=(int64_t (*)(CFTypeRef,uint32_t))dlsym(RTLD_DEFAULT,"CGEventGetIntegerValueField");
static auto setScalar=(void (*)(CFTypeRef,uint32_t,double))dlsym(RTLD_DEFAULT,"CGEventSetDoubleValueField");
static auto setInteger=(void (*)(CFTypeRef,uint32_t,int64_t))dlsym(RTLD_DEFAULT,"CGEventSetIntegerValueField");
static auto timestamp=(void (*)(CFTypeRef,uint64_t))dlsym(RTLD_DEFAULT,"CGEventSetTimestamp");
static CFTypeRef CG(id event) {
    return event&&[event respondsToSelector:sel_registerName("CGEvent")]?
        ((CFTypeRef (*)(id,SEL))objc_msgSend)(event,sel_registerName("CGEvent")):nullptr;
}
static bool Ready() {return copy&&scalar&&integer&&setScalar&&setInteger&&timestamp;}
static bool Replay(id event) {
    CFTypeRef cg=CG(event);return cg&&integer&&(uint64_t)integer(cg,42)==replayMarker;
}
struct Quantizer {
    double residualX=0,residualY=0;
    double take(double value,double &residual,double units) {
        double total=value+residual,output=std::round(total*units)/units;
        residual=total-output;return output;
    }
    void apply(double &x,double &y,double units) {
        x=take(x,residualX,units);y=take(y,residualY,units);
    }
};
static id InWindow(id event,id source,id window) {
    if(!window)return event;
    // Quartz-to-NSEvent reconstruction drops NSWindow ownership. Resolve it
    // with AppKit's own retargeter, then validate its public coordinates.
    // A missing/changed implementation fails back to the original wheel.
    SEL relative=sel_registerName("_eventRelativeToWindow:");
    if(![event respondsToSelector:relative])return nil;
    CGPoint expected=((CGPoint (*)(id,SEL))objc_msgSend)(source,sel_registerName("locationInWindow"));
    id origin=((id (*)(id,SEL))objc_msgSend)(source,sel_registerName("window"));
    if(origin!=window){
        if(origin)expected=((CGPoint (*)(id,SEL,CGPoint))objc_msgSend)(origin,sel_registerName("convertPointToScreen:"),expected);
        expected=((CGPoint (*)(id,SEL,CGPoint))objc_msgSend)(window,sel_registerName("convertPointFromScreen:"),expected);
    }
    id result=((id (*)(id,SEL,id))objc_msgSend)(event,relative,window);
    if(!result||((id (*)(id,SEL))objc_msgSend)(result,sel_registerName("window"))!=window)return nil;
    CGPoint point=((CGPoint (*)(id,SEL))objc_msgSend)(result,sel_registerName("locationInWindow"));
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||std::fabs(point.x-expected.x)>.01||std::fabs(point.y-expected.y)>.01)return nil;
    return result;
}
static id Make(id source,double x,double y,double pointX,double pointY,double now,id window=nil) {
    if(!Ready()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(pointX)||
       !std::isfinite(pointY)||!std::isfinite(now)||now<0)return nil;
    CFTypeRef original=CG(source);if(!original)return nil;
    CFTypeRef cg=copy(original);if(!cg)return nil;
    // DeltaAxis is integral; set it BEFORE FixedPtDelta (16.16), otherwise
    // Quartz replaces the fractional line distance with its integer value.
    setInteger(cg,11,(int64_t)y);setInteger(cg,12,(int64_t)x);
    setInteger(cg,88,0);setInteger(cg,99,0);setInteger(cg,123,0);
    setInteger(cg,42,(int64_t)replayMarker);timestamp(cg,(uint64_t)(now*1e9));
    setScalar(cg,93,y);setScalar(cg,94,x);
    setScalar(cg,96,pointY);setScalar(cg,97,pointX);
    id result=((id (*)(id,SEL,CFTypeRef))objc_msgSend)(NSClassFromString(@"NSEvent"),sel_registerName("eventWithCGEvent:"),cg);
    CFRelease(cg);return InWindow(result,source,window);
}
}
