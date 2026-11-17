#pragma once
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <objc/message.h>
#import <objc/runtime.h>

// NSCursor owns both the image and its physical click hotspot. Its image is
// independent of UIKit/Unity scaling and stays above embedded web content.
static CGPoint DesktopNativeCursorHotspot(CGSize size,CGPoint requested) {
    // The supplied Genshin arrow's visible tip starts after its 2px padding.
    if(CGPointEqualToPoint(requested,CGPointZero) && CGSizeEqualToSize(size,CGSizeMake(32,32)))
        return CGPointMake(2,2);
    return requested;
}
static id MakeDesktopNativeCursor(NSData *png,CGSize size,CGPoint hotspot) {
    Class imageClass=NSClassFromString(@"NSImage"),cursorClass=NSClassFromString(@"NSCursor");
    if(!imageClass || !cursorClass || !png) return nil;
    id image=((id (*)(id,SEL))objc_msgSend)(imageClass,sel_registerName("alloc"));
    image=((id (*)(id,SEL,id))objc_msgSend)(image,sel_registerName("initWithData:"),png);
    if(!image) return nil;
    ((void (*)(id,SEL,CGSize))objc_msgSend)(image,sel_registerName("setSize:"),size);
    id cursor=((id (*)(id,SEL))objc_msgSend)(cursorClass,sel_registerName("alloc"));
    return ((id (*)(id,SEL,id,CGPoint))objc_msgSend)(cursor,sel_registerName("initWithImage:hotSpot:"),image,hotspot);
}
static void ApplyDesktopNativeCursor(id cursor,void (*setter)(id,SEL),bool force) {
    if(!cursor || !setter) return;
    id current=((id (*)(id,SEL))objc_msgSend)(NSClassFromString(@"NSCursor"),sel_registerName("currentCursor"));
    // AppKit's cached NSCursor object can stay unchanged while UIKit changes
    // the WindowServer image during dispatch. Reassert after those events.
    if(force || current!=cursor) setter(cursor,sel_registerName("set"));
}
