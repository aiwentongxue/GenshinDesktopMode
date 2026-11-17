#pragma once
#include "native_web_visibility.h"
#include "web_escape_policy.h"

// Use the SDK's registered close control or its explicit close-button action.
// Notice and wish history are eligible; account/authentication pages are not.
struct DesktopWebEscapeTarget {
    __unsafe_unretained UIView *host=nil;
    __unsafe_unretained UIControl *button=nil;
    SEL action=nullptr;
    bool wish=false;
    explicit operator bool() const {return host && (button || action);}
};
static NSString *DesktopWebLocationPath(id object,const char *getter) {
    SEL selector=sel_registerName(getter);
    if(![object respondsToSelector:selector]) return nil;
    Method method=class_getInstanceMethod([object class],selector);
    char type[8]={};if(method) method_getReturnType(method,type,sizeof(type));
    if(type[0]!='@') return nil;
    id value=((id (*)(id,SEL))objc_msgSend)(object,selector);
    NSURL *url=[value isKindOfClass:NSURL.class]?value:
        [value isKindOfClass:NSString.class]?[NSURL URLWithString:value]:nil;
    return url.path.lowercaseString;
}
static bool DesktopWishHistoryLocation(id object,const char *getter) {
    return IsDesktopWishHistoryPath(DesktopWebLocationPath(object,getter).UTF8String);
}
static void DescribeDesktopWebEscapeHost(id web) {
    // Paths only; never log full URLs, auth queries, page text or account data.
    NSString *path=DesktopWebLocationPath(web,"URL");
    Log(@"web_escape_page class=%@ path=%@ wish=%d",NSStringFromClass([web class]),path?:@"",IsDesktopWishHistoryPath(path.UTF8String));
    id view=web;
    for(unsigned i=0;view && i<12;++i) {
        Log(@"web_escape_host depth=%u class=%@ close_button=%d sdk_action=%d notice=%d",i,NSStringFromClass([view class]),
            [view respondsToSelector:sel_registerName("closeBtn")],
            [view respondsToSelector:sel_registerName("clickCloseBtn")],
            [view respondsToSelector:sel_registerName("showNotice")]);
        if(![view respondsToSelector:sel_registerName("superview")]) break;
        view=((id (*)(id,SEL))objc_msgSend)(view,sel_registerName("superview"));
    }
}
static DesktopWebEscapeTarget FindDesktopWebEscapeTarget(UIView *view,unsigned &budget) {
    if(!view || !budget || view.hidden || view.alpha<0.02) return {};
    --budget;
    Class noticeClass=NSClassFromString(@"MiHoYoWebView");
    Class baseClass=NSClassFromString(@"MiHoYoBaseWebView");
    SEL notice=sel_registerName("showNotice");
    if((noticeClass && [view isKindOfClass:noticeClass]) || (baseClass && [view isKindOfClass:baseClass])) {
        bool isNotice=false;
        if([view respondsToSelector:notice]) {
            Method method=class_getInstanceMethod(view.class,notice);
            char type[8]={};if(method) method_getReturnType(method,type,sizeof(type));
            isNotice=(type[0]=='B' || type[0]=='c') && ((BOOL (*)(id,SEL))objc_msgSend)(view,notice);
        }
        unsigned webBudget=128;
        id web=FindDesktopWebView(view,webBudget,false);
        bool wish=DesktopWishHistoryLocation(view,"url") || DesktopWishHistoryLocation(view,"loadingURL") ||
            (web && DesktopWishHistoryLocation(web,"URL"));
        if(web && (isNotice || wish)) {
            for(const char *name:{"fullScreenCloseBtn","closeBtn"}) {
                SEL getter=sel_registerName(name);if(![view respondsToSelector:getter]) continue;
                id button=((id (*)(id,SEL))objc_msgSend)(view,getter);
                if(![button isKindOfClass:UIControl.class] || ![(UIControl *)button isEnabled]) continue;
                for(id target in [(UIControl *)button allTargets]) {
                    NSArray *actions=[(UIControl *)button actionsForTarget:target forControlEvent:UIControlEventTouchUpInside];
                    for(NSString *action in actions)
                        if([action.lowercaseString containsString:@"close"]) return {view,button,nullptr,wish};
                }
            }
            // Some wish-record layouts draw their own HTML close icon while
            // keeping the same SDK close action behind it.
            if(wish) {
                SEL close=sel_registerName("clickCloseBtn");
                Method method=class_getInstanceMethod(view.class,close);
                char type[8]={};if(method) method_getReturnType(method,type,sizeof(type));
                if(method && method_getNumberOfArguments(method)==2 && type[0]=='v')
                    return {view,nil,close,true};
            }
        }
    }
    for(UIView *child in view.subviews.reverseObjectEnumerator)
        if(auto target=FindDesktopWebEscapeTarget(child,budget)) return target;
    return {};
}
static DesktopWebEscapeTarget DesktopWebEscapeAction(void) {
    for(UIWindow *window in UIApplication.sharedApplication.windows.reverseObjectEnumerator) {
        if(window.hidden || window.alpha<0.02) continue;
        unsigned budget=256;
        if(auto target=FindDesktopWebEscapeTarget(window,budget)) return target;
    }
    return {};
}
static bool CloseDesktopNoticeOnEscape(void (*beforeClose)(void)) {
    auto target=DesktopWebEscapeAction();if(!target) return false;
    if(beforeClose) beforeClose();
    if(target.button) [target.button sendActionsForControlEvents:UIControlEventTouchUpInside];
    else ((void (*)(id,SEL))objc_msgSend)(target.host,target.action);
    Log(@"web_escape_close page=%s action=%s",target.wish?"wish_history":"notice",target.button?"registered_control":"sdk_close_button");
    return true;
}
