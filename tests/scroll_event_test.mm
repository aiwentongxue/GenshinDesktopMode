#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include "../genshin/scroll_event.h"
#include <cassert>
#include <iostream>

int main() {@autoreleasepool {
    DesktopScrollEventSequence sequence;
    CGEventRef raw=CGEventCreateScrollWheelEvent(nullptr,kCGScrollEventUnitLine,1,1);
    // Mos rewrites point deltas while retaining the raw event's line fields.
    // Reading deltaY would replay that original whole step on every pulse.
    CGEventSetDoubleValueField(raw,kCGScrollWheelEventPointDeltaAxis1,2);
    CGEventSetIntegerValueField(raw,kCGScrollWheelEventIsContinuous,1);
    NSEvent *first=[NSEvent eventWithCGEvent:raw];
    auto key=DesktopScrollKey(first);
    assert(key.precise && key.y==2 && first.deltaY==1);
    assert(!DesktopScrollIsMos(first));
    assert(sequence.first(first));assert(!sequence.first(first));
    CGEventSetIntegerValueField(raw,kCGEventSourceUserData,0x4D4F53534D4F4F54LL);
    for(int i=0;i<100;++i) {
        CGEventRef copy=CGEventCreateCopy(raw);
        NSEvent *next=[NSEvent eventWithCGEvent:copy];
        assert(DesktopScrollKey(next)==key);
        assert(DesktopScrollIsMos(next));
        assert(sequence.first(next));assert(!sequence.first(next));
        CFRelease(copy);
    }
    CFRelease(raw);
    // Ordinary packets may have identical timestamps and deltas while GC
    // produces no second changed-value callback. Feed each distinct NSEvent
    // exactly once to the actual selection router. Fractional real-device
    // packets are separately replayed in interaction_wheel_router_test.
    DesktopWheelRouter selection;
    raw=CGEventCreateScrollWheelEvent(nullptr,kCGScrollEventUnitLine,1,1);
    for(int i=0;i<12;++i) {
        CGEventRef copy=CGEventCreateCopy(raw);
        NSEvent *next=[NSEvent eventWithCGEvent:copy];
        auto packet=DesktopScrollKey(next);
        assert(!packet.precise && packet.y==1);
        assert(sequence.first(next));assert(!sequence.first(next));
        selection.system(packet.stamp,10+i,packet.x,packet.y,packet.precise,
            packet.phase,packet.momentum,1,false,0,
            CGEventGetIntegerValueField(next.CGEvent,kCGScrollWheelEventDeltaAxis1));
        assert(selection.frame(10+i,1,1,true).y==1);
        CFRelease(copy);
    }
    CFRelease(raw);
    std::cout<<"Real NSEvent: precise point deltas selected; same-object duplicates skipped; equal-timestamp Mos copies retained\n";
    std::cout<<"PASS real ordinary NSEvent: twelve identical packets delivered as twelve selection steps without GC callbacks\n";
}}
