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
static NSString *desktopWebCursorRule;
static NSUInteger desktopWebPolicyRevision=1;
static void ConfigureDesktopWebCursor(NSData *png,CGPoint hotspot) {
    NSString *rule=[NSString stringWithFormat:@"cursor:url(\"data:image/png;base64,%@\") %.0f %.0f,default!important;",
        [png base64EncodedStringWithOptions:0],hotspot.x,hotspot.y];
    if([rule isEqualToString:desktopWebCursorRule]) return;
    desktopWebCursorRule=rule;++desktopWebPolicyRevision;
}
static NSString *DesktopWebSelectionScript(void) {
    NSString *source=@"(()=>{const apply=()=>{"
        "if(!document.documentElement)return;"
        "let s=document.getElementById('__gdm_no_selection_r28');"
        "if(!s){s=document.createElement('style');s.id='__gdm_no_selection_r28';"
        "document.documentElement.appendChild(s);}"
        "s.textContent='html,body,body *:not(input):not(textarea):not(select):not(option):not([contenteditable=true]){user-select:none!important;-webkit-user-select:none!important;__GDM_CURSOR_RULE__}input,textarea,[contenteditable=true],[contenteditable=true] *{user-select:text!important;-webkit-user-select:text!important}select,option{user-select:auto!important;-webkit-user-select:auto!important;cursor:default!important}';"
        "const edit=t=>t&&t.closest&&t.closest('input,textarea,select,option,[role=combobox],[contenteditable=true]');"
        "if(!edit(document.activeElement)){const q=window.getSelection();if(q)q.removeAllRanges();}"
        "if(!window.__gdmSelectionGuard_r28){window.__gdmSelectionGuard_r28=true;"
        "document.addEventListener('selectstart',e=>{if(!edit(e.target))e.preventDefault();},true);}"
        "};apply();if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',apply,{once:true});return true;})()";
    return [source stringByReplacingOccurrencesOfString:@"__GDM_CURSOR_RULE__" withString:desktopWebCursorRule?:@""];
}
static bool PrepareDesktopWebView(id web) {
    static char policyKey;
    NSNumber *previous=objc_getAssociatedObject(web,&policyKey);
    if(previous.unsignedIntegerValue==desktopWebPolicyRevision) return false;
    NSString *source=DesktopWebSelectionScript();
    Class scriptClass=NSClassFromString(@"WKUserScript");
    if(scriptClass && [web respondsToSelector:sel_registerName("configuration")]) {
        id configuration=((id (*)(id,SEL))objc_msgSend)(web,sel_registerName("configuration"));
        id controller=((id (*)(id,SEL))objc_msgSend)(configuration,sel_registerName("userContentController"));
        id script=((id (*)(id,SEL))objc_msgSend)(scriptClass,sel_registerName("alloc"));
        script=((id (*)(id,SEL,id,NSInteger,BOOL))objc_msgSend)(script,
            sel_registerName("initWithSource:injectionTime:forMainFrameOnly:"),source,1,NO);
        if(controller && script) ((void (*)(id,SEL,id))objc_msgSend)(controller,sel_registerName("addUserScript:"),script);
    }
    ((void (*)(id,SEL,id,id))objc_msgSend)(web,sel_registerName("evaluateJavaScript:completionHandler:"),source,nil);
    objc_setAssociatedObject(web,&policyKey,@(desktopWebPolicyRevision),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return true;
}
static NSHashTable *desktopWebViews;
static void (*originalDesktopWebMoved)(id,SEL);
static id (*originalDesktopWebRequest)(id,SEL,id);
static void RegisterDesktopWebView(id web) {
    if(!web) return;
    if(!desktopWebViews) desktopWebViews=[NSHashTable weakObjectsHashTable];
    [desktopWebViews addObject:web];PrepareDesktopWebView(web);
}
static void MovedDesktopWebView(id self,SEL selector) {
    originalDesktopWebMoved(self,selector);
    if(((id (*)(id,SEL))objc_msgSend)(self,sel_registerName("window"))) RegisterDesktopWebView(self);
}
static id RequestDesktopWebView(id self,SEL selector,id request) {
    RegisterDesktopWebView(self);return originalDesktopWebRequest(self,selector,request);
}
static void InstallDesktopWebHooks(void) {
    Class cls=NSClassFromString(@"WKWebView");if(!cls) return;
    if(!originalDesktopWebMoved) {
        for(const char *name:{"didMoveToWindow","viewDidMoveToWindow"}) {
            SEL selector=sel_registerName(name);Method method=class_getInstanceMethod(cls,selector);
            if(!method) continue;
            originalDesktopWebMoved=(void (*)(id,SEL))method_getImplementation(method);
            class_replaceMethod(cls,selector,(IMP)MovedDesktopWebView,method_getTypeEncoding(method));break;
        }
    }
    if(!originalDesktopWebRequest) {
        SEL selector=sel_registerName("loadRequest:");Method method=class_getInstanceMethod(cls,selector);
        if(method) {
            originalDesktopWebRequest=(id (*)(id,SEL,id))method_getImplementation(method);
            class_replaceMethod(cls,selector,(IMP)RequestDesktopWebView,method_getTypeEncoding(method));
        }
    }
}
static id RegisteredDesktopWebView(void) {
    for(id web in desktopWebViews.allObjects) {
        id window=((id (*)(id,SEL))objc_msgSend)(web,sel_registerName("window"));if(!window) continue;
        if([window respondsToSelector:sel_registerName("isVisible")] &&
           !((BOOL (*)(id,SEL))objc_msgSend)(window,sel_registerName("isVisible"))) continue;
        bool appKit=[web respondsToSelector:sel_registerName("visibleRect")];
        id parent=web;bool visible=true;
        for(unsigned depth=0;parent && depth<64;++depth) {
            if([parent respondsToSelector:sel_registerName("isHidden")] &&
               ((BOOL (*)(id,SEL))objc_msgSend)(parent,sel_registerName("isHidden"))) {visible=false;break;}
            SEL alpha=sel_registerName(appKit?"alphaValue":"alpha");
            if([parent respondsToSelector:alpha] && ((CGFloat (*)(id,SEL))objc_msgSend)(parent,alpha)<0.02) {visible=false;break;}
            parent=[parent respondsToSelector:sel_registerName("superview")]?
                ((id (*)(id,SEL))objc_msgSend)(parent,sel_registerName("superview")):nil;
        }
        if(visible) {unsigned budget=1;if(FindDesktopWebView(web,budget,appKit)) return web;}
    }
    return nil;
}
