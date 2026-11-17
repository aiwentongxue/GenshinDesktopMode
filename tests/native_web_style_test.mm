#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#include "../genshin/native_web_style.h"
#include <cassert>
#include <iostream>

@interface DesktopStyleNavigation : NSObject <WKNavigationDelegate>
@property bool done;
@end
@implementation DesktopStyleNavigation
- (void)webView:(WKWebView *)web didFinishNavigation:(WKNavigation *)navigation {self.done=true;}
@end
static void WaitUntil(bool (^ready)(void)) {
    NSDate *end=[NSDate dateWithTimeIntervalSinceNow:10];
    while(!ready() && end.timeIntervalSinceNow>0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    assert(ready());
}
static void CheckDocument(WKWebView *web) {
    __block bool done=false;
    [web evaluateJavaScript:@"(()=>{const l=document.querySelector('label'),f=document.querySelector('input'),s=document.querySelector('select'),b=document.querySelector('button');"
        "const sel=t=>{const e=new Event('selectstart',{bubbles:true,cancelable:true});t.dispatchEvent(e);return e.defaultPrevented;};"
        "const click=t=>{const e=new MouseEvent('click',{bubbles:true,cancelable:true});t.dispatchEvent(e);return e.defaultPrevented;};"
        "return [l,f,s,b].every(t=>getComputedStyle(t).cursor.includes('data:image/png;base64,'))&&getComputedStyle(l).webkitUserSelect==='none'&&getComputedStyle(f).webkitUserSelect==='text'&&sel(l)&&!sel(f)&&!sel(s)&&!click(s)&&!click(b);})()"
        completionHandler:^(id result,NSError *error) {assert(!error && [result boolValue]);done=true;}];
    WaitUntil(^bool {return done;});
}
static void CheckNestedDocument(WKWebView *web) {
    __block bool done=false;
    [web evaluateJavaScript:@"(()=>{const w=document.querySelector('iframe').contentWindow;return w.getComputedStyle(w.document.querySelector('label')).cursor.includes('data:image/png;base64,')&&w.getComputedStyle(w.document.querySelector('label')).webkitUserSelect==='none';})()"
        completionHandler:^(id result,NSError *error) {assert(!error && [result boolValue]);done=true;}];
    WaitUntil(^bool {return done;});
}
static void CheckDynamicRepair(WKWebView *web) {
    __block bool changed=false;
    [web evaluateJavaScript:@"(()=>{document.getElementById('__gdm_web_style_r31').remove();"
        "const late=document.createElement('style');late.textContent='body button{cursor:none!important}';document.head.appendChild(late);"
        "const l=document.querySelector('label');l.style.setProperty('cursor','none','important');l.dispatchEvent(new Event('pointerover',{bubbles:true}));return true;})()"
        completionHandler:^(id result,NSError *error){assert(!error);changed=true;}];
    WaitUntil(^bool{return changed;});
    CheckDocument(web); // no native re-evaluation: the document repairs itself
    assert(PrepareDesktopWebStyle(web,true));
    assert(web.configuration.userContentController.userScripts.count==1);
}
int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        NSData *png=[[NSData alloc] initWithBase64EncodedString:@"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j9YkAAAAASUVORK5CYII=" options:0];
        ConfigureDesktopWebStyle(png,CGSizeMake(32,32),CGPointZero);
        assert(desktopWebGoldCursor);
        NSCursor *cursor=desktopWebGoldCursor;
        assert(NSEqualPoints(cursor.hotSpot,NSMakePoint(2,2)));
        assert(NSEqualSizes(cursor.image.size,NSMakeSize(32,32)));
        NSUInteger revision=desktopWebStyleRevision;
        ConfigureDesktopWebStyle(nil,CGSizeMake(32,32),CGPointZero);
        assert(desktopWebStyleRevision==revision);
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,800,600)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        WKWebView *web=[[WKWebView alloc] initWithFrame:NSMakeRect(0,0,800,600)];
        [window.contentView addSubview:web];
        assert(PrepareDesktopWebStyle(web));assert(!PrepareDesktopWebStyle(web));
        assert(web.configuration.userContentController.userScripts.count==1);
        DesktopStyleNavigation *navigation=[DesktopStyleNavigation new];web.navigationDelegate=navigation;
        NSString *html=@"<html><body><label>Page 2</label><input value='editable'><select><option>one</option><option>two</option></select><button>next</button></body></html>";
        [web loadHTMLString:html baseURL:nil];WaitUntil(^bool{return navigation.done;});CheckDocument(web);
        CheckDynamicRepair(web);
        // A second real document verifies that the page policy survives reload.
        navigation.done=false;
        [web loadHTMLString:html baseURL:nil];WaitUntil(^bool{return navigation.done;});CheckDocument(web);
        assert(!PrepareDesktopWebStyle(web));
        assert(web.configuration.userContentController.userScripts.count==1);
        // A new SDK-created sibling needs the policy before its first load.
        assert(InstallDesktopWebStyleHooks());
        WKWebView *sdk=[[WKWebView alloc] initWithFrame:NSMakeRect(0,0,400,300)];
        [window.contentView addSubview:sdk];
        NSMutableArray *webs=[NSMutableArray array];unsigned budget=256;
        CollectDesktopWebViews(window.contentView,budget,true,webs);
        assert(webs.count==2 && [webs containsObject:web] && [webs containsObject:sdk]);
        sdk.hidden=YES;[webs removeAllObjects];budget=256;
        CollectDesktopWebViews(window.contentView,budget,true,webs);assert(webs.count==1);sdk.hidden=NO;
        DesktopStyleNavigation *sdkNavigation=[DesktopStyleNavigation new];sdk.navigationDelegate=sdkNavigation;
        NSString *nested=@"<html><body><iframe srcdoc='<html><body><label>Nested page</label></body></html>'></iframe></body></html>";
        [sdk loadHTMLString:nested baseURL:nil];WaitUntil(^bool{return sdkNavigation.done;});
        assert(sdk.configuration.userContentController.userScripts.count==1);
        assert(!sdk.configuration.userContentController.userScripts.firstObject.forMainFrameOnly);
        CheckNestedDocument(sdk);
        // The late-apply path also reaches a frame which already exists.
        __block bool removed=false;
        [sdk evaluateJavaScript:@"document.querySelector('iframe').contentDocument.getElementById('__gdm_web_style_r31').remove();true"
            completionHandler:^(id result,NSError *error){assert(!error);removed=true;}];
        WaitUntil(^bool{return removed;});
        __block bool reapplied=false;
        [sdk evaluateJavaScript:DesktopWebStyleScript() completionHandler:^(id result,NSError *error){assert(!error);reapplied=true;}];
        WaitUntil(^bool{return reapplied;});CheckNestedDocument(sdk);
        std::cout<<"WebKit: gold cursor survives removed styles, late CSS and inline hidden cursor; cached-view refresh adds no duplicate scripts; selection/clicks/reload/all-frame policy and hotspot passed\n";
    }
}
