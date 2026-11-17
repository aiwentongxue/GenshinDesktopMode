#pragma once
#include "native_scroll_packet.h"
#include "coarse_scroll_state.h"

namespace DesktopWebWheel {
static void Tick();
static CADisplayLink *display;
static id driver;
static __weak id targetWindow,targetView,overlayWindow;
static NSHashTable *webWindows;
static NSArray *webTokens;
static id templateEvent;
static CGPoint targetLocation;
static uint64_t revision=1;
static DesktopCoarseScrollState lines,points;
static DesktopNativeScrollPacket::Quantizer lineQuantizer,pointQuantizer;
static void Cancel() {
    lines.cancel();points.cancel();templateEvent=nil;targetWindow=targetView=nil;
    lineQuantizer={};pointQuantizer={};display.paused=YES;
}
static void Views(NSArray *webs) {
    NSMutableArray *tokens=[NSMutableArray arrayWithCapacity:webs.count];
    for(id web in webs){
        id url=[web respondsToSelector:sel_registerName("URL")]?
            ((id (*)(id,SEL))objc_msgSend)(web,sel_registerName("URL")):nil;
        [tokens addObject:[NSString stringWithFormat:@"%p:%@",(__bridge void *)web,url?:@""]];
    }
    if([webTokens isEqual:tokens])return;
    Cancel();++revision;webTokens=[tokens copy];
    overlayWindow=webs.count?NativeGameWindow():nil;
    webWindows=[NSHashTable weakObjectsHashTable];
    for(id web in webs){
        // AppKit web views expose their actual NSWindow. UIKit SDK pages use
        // the key host captured at the moment the web view appears.
        if([web respondsToSelector:sel_registerName("visibleRect")]){
            id window=((id (*)(id,SEL))objc_msgSend)(web,sel_registerName("window"));
            if(window)[webWindows addObject:window];
        }
    }
}
static id Hit(id window,CGPoint point) {
    id root=((id (*)(id,SEL))objc_msgSend)(window,sel_registerName("contentView"));
    if(!root)return nil;
    CGPoint local=((CGPoint (*)(id,SEL,CGPoint,id))objc_msgSend)(root,sel_registerName("convertPoint:fromView:"),point,nil);
    return ((id (*)(id,SEL,CGPoint))objc_msgSend)(root,sel_registerName("hitTest:"),local);
}
static bool Allowed(id window) {
    return window&&NativeBridge::nativeOverlayActive.load()&&!pointerCaptureSuspended&&
        DesktopWebAppIsActive()&&(window==overlayWindow||[webWindows containsObject:window])&&
        ((BOOL (*)(id,SEL))objc_msgSend)(window,sel_registerName("isVisible"));
}
static bool Handle(id window,id event,const DesktopScrollDispatchScope::Key &key);
}

@interface DesktopWebWheelDisplay : NSObject
- (void)frame:(CADisplayLink *)link;
@end
@implementation DesktopWebWheelDisplay
- (void)frame:(CADisplayLink *)link {DesktopWebWheel::Tick();}
@end

namespace DesktopWebWheel {
static void Start() {
    if(!display){
        driver=[DesktopWebWheelDisplay new];
        display=[CADisplayLink displayLinkWithTarget:driver selector:@selector(frame:)];
        display.preferredFramesPerSecond=120;
        [display addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    }
    display.paused=NO;
}
static bool Handle(id window,id event,const DesktopScrollDispatchScope::Key &key) {
    if(DesktopNativeScrollPacket::Replay(event))return false;
    NSUInteger modifiers=((NSUInteger (*)(id,SEL))objc_msgSend)(event,sel_registerName("modifierFlags"));
    if(!Allowed(window)||key.precise||key.phase||key.momentum||DesktopScrollIsMos(event)||(modifiers&0x1e0000)){
        Cancel();return false;
    }
    if(!std::isfinite(key.x)||!std::isfinite(key.y)||(!key.x&&!key.y)||!DesktopNativeScrollPacket::Ready())return false;
    CGPoint location=((CGPoint (*)(id,SEL))objc_msgSend)(event,sel_registerName("locationInWindow"));
    if(!CGRectContainsPoint(NativeGameContentRect(window),location)) {Cancel();return false;}
    id hit=Hit(window,location);if(!hit)return false;
    CFTypeRef cg=DesktopNativeScrollPacket::CG(event);if(!cg)return false;
    // NSEvent may already transpose the axes for Shift. Reconstruct the
    // original Quartz axes so the preserved modifiers are applied only once.
    double lx=DesktopNativeScrollPacket::scalar(cg,94),ly=DesktopNativeScrollPacket::scalar(cg,93);
    double px=DesktopNativeScrollPacket::scalar(cg,97),py=DesktopNativeScrollPacket::scalar(cg,96);
    // Validate construction before consuming the original event.
    id sample=DesktopNativeScrollPacket::Make(event,lx,ly,px,py,CACurrentMediaTime(),window);
    if(!sample)return false;
    if(window!=targetWindow||hit!=targetView||std::hypot(location.x-targetLocation.x,location.y-targetLocation.y)>3){Cancel();++revision;}
    targetWindow=window;targetView=hit;templateEvent=event;
    targetLocation=location;
    double now=CACurrentMediaTime();
    lines.event(now,lx,ly,revision);points.event(now,px,py,revision);
    Start();
    static unsigned count=0;
    if(++count<=4)Log(@"web_wheel_smooth coarse=1 display_synced=1 original_line_units=1 packet=%u",count);
    return true;
}
static void Tick() {
    id window=targetWindow;
    if(!templateEvent||!targetView||!Allowed(window)){Cancel();return;}
    CGPoint location=((CGPoint (*)(id,SEL))objc_msgSend)(window,sel_registerName("mouseLocationOutsideOfEventStream"));
    if(Hit(window,location)!=targetView||std::hypot(location.x-targetLocation.x,location.y-targetLocation.y)>3){Cancel();return;}
    double now=CACurrentMediaTime();
    auto line=lines.frame(now,revision),point=points.frame(now,revision);
    lineQuantizer.apply(line.x,line.y,65536);pointQuantizer.apply(point.x,point.y,1);
    if(line.x||line.y||point.x||point.y){
        id event=DesktopNativeScrollPacket::Make(templateEvent,line.x,line.y,point.x,point.y,now,window);
        if(!event){Cancel();return;}
        // Direct original window delivery is local to this SDK view. Never
        // post to macOS, reenter the application's input observer, or modify
        // its cursor, click, selection, drag or Escape handling.
        originalWindowSendEvent(window,sel_registerName("sendEvent:"),event);
        static uint64_t frames=0;
        if(++frames<=3)Log(@"web_wheel_frame frame=%llu line=(%.5f,%.5f) points=(%.0f,%.0f) local_window=1",frames,line.x,line.y,point.x,point.y);
    }
    if(!lines.pending()&&!points.pending())Cancel();
}
}
