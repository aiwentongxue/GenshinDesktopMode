#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include "../genshin/native_scroll_packet.h"
#include "../genshin/coarse_scroll_state.h"
#include <cassert>
#include <cstdio>
#include <vector>
static void Wait(double seconds){NSDate *end=[NSDate dateWithTimeIntervalSinceNow:seconds];while(end.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.002]];}
static id JS(WKWebView *web,NSString *source){__block bool done=false;__block id value;__block NSError *error;[web evaluateJavaScript:source completionHandler:^(id r,NSError *e){value=r;error=e;done=true;}];NSDate *end=[NSDate dateWithTimeIntervalSinceNow:8];while(!done&&end.timeIntervalSinceNow>0)Wait(.005);assert(done&&!error);return value;}
static NSEvent *Wheel(NSWindow *window,double x,double y){
 CGEventRef cg=CGEventCreateScrollWheelEvent(nullptr,kCGScrollEventUnitLine,1,-3);
 CGPoint screen=[window convertPointToScreen:CGPointMake(x,y)];CGFloat height=NSScreen.screens.firstObject.frame.size.height;
 CGEventSetLocation(cg,CGPointMake(screen.x,height-screen.y));
 NSEvent *raw=[NSEvent eventWithCGEvent:cg];id result=DesktopNativeScrollPacket::InWindow(raw,raw,window);CFRelease(cg);assert(result);return result;
}
int main(){@autoreleasepool{
 [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
 NSWindow *window=[[NSWindow alloc]initWithContentRect:NSMakeRect(-10000,-10000,800,600) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
 WKWebView *web=[[WKWebView alloc]initWithFrame:NSMakeRect(0,0,800,600)];[window.contentView addSubview:web];[window orderFront:nil];[window makeFirstResponder:web];
 [web loadHTMLString:@"<html><body style='margin:0;overflow:hidden'><div id='left' style='position:absolute;left:0;top:0;width:400px;height:600px;overflow:auto'><div style='height:10000px'>left</div></div><div id='right' style='position:absolute;left:400px;top:0;width:400px;height:600px;overflow:auto'><div style='height:10000px'>right</div></div></body></html>" baseURL:nil];
 Wait(.8);assert([JS(web,@"!!document.getElementById('right')") boolValue]);
 NSEvent *source=Wheel(window,600,300);[window sendEvent:source];Wait(.25);
 NSArray *raw=JS(web,@"[left.scrollTop,right.scrollTop]");assert([raw[0] doubleValue]==0&&[raw[1] doubleValue]>0);
 JS(web,@"left.scrollTop=right.scrollTop=0;true");Wait(.1);
 DesktopCoarseScrollState line,point;DesktopNativeScrollPacket::Quantizer lq,pq;
 double start=NSProcessInfo.processInfo.systemUptime;line.event(start,0,-3,1);point.event(start,0,-30,1);
 std::vector<double> samples;double emitted=0;
 for(int i=0;i<30;++i){double now=start+i/120.;auto l=line.frame(now,1),p=point.frame(now,1);lq.apply(l.x,l.y,65536);pq.apply(p.x,p.y,1);
  if(l.y||p.y){NSEvent *part=DesktopNativeScrollPacket::Make(source,l.x,l.y,p.x,p.y,now,window);assert(part&&part.window==window&&NSEqualPoints(part.locationInWindow,source.locationInWindow));emitted+=part.scrollingDeltaY;[window sendEvent:part];}
  Wait(1./120.);samples.push_back([JS(web,@"right.scrollTop") doubleValue]);
 }
 Wait(.15);NSArray *after=JS(web,@"[left.scrollTop,right.scrollTop]");double old=[raw[1] doubleValue],smooth=[after[1] doubleValue];
 unsigned changes=0;for(size_t i=1;i<samples.size();++i){assert(samples[i]>=samples[i-1]);if(samples[i]>samples[i-1])++changes;}
 assert(std::fabs(emitted+3)<1e-9&&[after[0] doubleValue]==0&&std::fabs(old-smooth)<=2&&changes>=3);
 assert(source.scrollingDeltaY==-3);[window orderOut:nil];
 printf("Real NSWindow/WebKit: raw=%.0fpx reconstructed=%.0fpx, %u progressive frames, correct right-hand container/window coordinates; no OS posting\n",old,smooth,changes);
}}
