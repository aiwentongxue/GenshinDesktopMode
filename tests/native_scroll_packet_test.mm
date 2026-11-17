#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include "../genshin/native_scroll_packet.h"
#include <cassert>
#include <cstdio>
int main(){@autoreleasepool{
 CGEventRef raw=CGEventCreateScrollWheelEvent(nullptr,kCGScrollEventUnitLine,2,3,1);
 CGEventSetLocation(raw,CGPointMake(123,456));
 NSEvent *source=[NSEvent eventWithCGEvent:raw];
 DesktopNativeScrollPacket::Quantizer q,pointQ;
 double emittedX=0,emittedY=0,pointTotalX=0,pointTotalY=0;
 for(int i=0;i<17;++i){double x=1./17,y=3./17,px=10./17,py=30./17;q.apply(x,y,65536);pointQ.apply(px,py,1);
  NSEvent *part=DesktopNativeScrollPacket::Make(source,x,y,px,py,1+i/120.);
  assert(part&&part.type==NSEventTypeScrollWheel&&!part.hasPreciseScrollingDeltas);
  assert(part.modifierFlags==source.modifierFlags);
  assert(CGPointEqualToPoint(CGEventGetLocation(part.CGEvent),CGEventGetLocation(raw)));
  assert(DesktopNativeScrollPacket::Replay(part));emittedX+=part.scrollingDeltaX;emittedY+=part.scrollingDeltaY;
  pointTotalX+=CGEventGetIntegerValueField(part.CGEvent,kCGScrollWheelEventPointDeltaAxis2);pointTotalY+=CGEventGetIntegerValueField(part.CGEvent,kCGScrollWheelEventPointDeltaAxis1);
 }
 assert(std::fabs(emittedX-1)<1e-9&&std::fabs(emittedY-3)<1e-9&&pointTotalX==10&&pointTotalY==30);
 assert(source.scrollingDeltaX==1&&source.scrollingDeltaY==3&&!DesktopNativeScrollPacket::Replay(source));
 assert(CGEventGetIntegerValueField(raw,kCGScrollWheelEventPointDeltaAxis1)==30);CFRelease(raw);
 std::puts("Native web wheel copies: fractional line/point totals, source immutability, location and local replay marker passed");
}}
