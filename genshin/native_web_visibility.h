#pragma once
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <initializer_list>

// The game can host WebKit in AppKit as well as in UIKit. Inspect both trees
// by public capabilities; do not assume an embedded web view is a UIView.
static id FindDesktopWebView(id view,unsigned &budget,bool appKit) {
    if(!view || !budget) return nil;
    --budget;
    if([view respondsToSelector:sel_registerName("isHidden")] &&
       ((BOOL (*)(id,SEL))objc_msgSend)(view,sel_registerName("isHidden"))) return nil;
    SEL alpha=sel_registerName(appKit?"alphaValue":"alpha");
    if([view respondsToSelector:alpha] && ((CGFloat (*)(id,SEL))objc_msgSend)(view,alpha)<0.02) return nil;
    if(appKit && [view respondsToSelector:sel_registerName("visibleRect")]) {
        CGRect visible=((CGRect (*)(id,SEL))objc_msgSend)(view,sel_registerName("visibleRect"));
        if(CGRectIsEmpty(visible)) return nil;
    }
    SEL evaluate=sel_registerName("evaluateJavaScript:completionHandler:");
    if([view respondsToSelector:evaluate] && [view respondsToSelector:sel_registerName("bounds")]) {
        CGRect bounds=((CGRect (*)(id,SEL))objc_msgSend)(view,sel_registerName("bounds"));
        bool intersects=true;
        if(!appKit && [view respondsToSelector:sel_registerName("window")]) {
            id window=((id (*)(id,SEL))objc_msgSend)(view,sel_registerName("window"));
            if(!window) intersects=false;
            else {
                CGRect rect=((CGRect (*)(id,SEL,CGRect,id))objc_msgSend)(view,sel_registerName("convertRect:toView:"),bounds,window);
                CGRect clip=((CGRect (*)(id,SEL))objc_msgSend)(window,sel_registerName("bounds"));
                intersects=CGRectIntersectsRect(rect,clip);
            }
        }
        if(intersects && bounds.size.width>40 && bounds.size.height>40) return view;
    }
    if([view respondsToSelector:sel_registerName("subviews")]) {
        NSArray *children=((id (*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
        for(id child in children) if(id web=FindDesktopWebView(child,budget,appKit)) return web;
    }
    return nil;
}

// SDK pages can contain several sibling web views. Visit every visible leaf
// web view, while pruning its private backing hierarchy to bound the scan.
static void CollectDesktopWebViews(id view,unsigned &budget,bool appKit,NSMutableArray *webs) {
    if(!view || !budget) return;
    --budget;
    if([view respondsToSelector:sel_registerName("isHidden")] &&
       ((BOOL (*)(id,SEL))objc_msgSend)(view,sel_registerName("isHidden"))) return;
    SEL alpha=sel_registerName(appKit?"alphaValue":"alpha");
    if([view respondsToSelector:alpha] && ((CGFloat (*)(id,SEL))objc_msgSend)(view,alpha)<0.02) return;
    if(appKit && [view respondsToSelector:sel_registerName("visibleRect")]) {
        CGRect visible=((CGRect (*)(id,SEL))objc_msgSend)(view,sel_registerName("visibleRect"));
        if(CGRectIsEmpty(visible)) return;
    }
    SEL evaluate=sel_registerName("evaluateJavaScript:completionHandler:");
    if([view respondsToSelector:evaluate] && [view respondsToSelector:sel_registerName("bounds")]) {
        CGRect bounds=((CGRect (*)(id,SEL))objc_msgSend)(view,sel_registerName("bounds"));
        bool intersects=true;
        if(!appKit && [view respondsToSelector:sel_registerName("window")]) {
            id window=((id (*)(id,SEL))objc_msgSend)(view,sel_registerName("window"));
            if(!window) intersects=false;
            else {
                CGRect rect=((CGRect (*)(id,SEL,CGRect,id))objc_msgSend)(view,sel_registerName("convertRect:toView:"),bounds,window);
                CGRect clip=((CGRect (*)(id,SEL))objc_msgSend)(window,sel_registerName("bounds"));
                intersects=CGRectIntersectsRect(rect,clip);
            }
        }
        if(intersects && bounds.size.width>40 && bounds.size.height>40) {
            if(![webs containsObject:view]) [webs addObject:view];
            return;
        }
    }
    if([view respondsToSelector:sel_registerName("subviews")]) {
        NSArray *children=((id (*)(id,SEL))objc_msgSend)(view,sel_registerName("subviews"));
        for(id child in children) CollectDesktopWebViews(child,budget,appKit,webs);
    }
}
