#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#include "../genshin/native_web_cursor.h"
#include <cassert>
#include <iostream>

@interface DesktopTestNavigation : NSObject <WKNavigationDelegate>
@property bool done;
@end
@implementation DesktopTestNavigation
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
    [web evaluateJavaScript:@"(()=>{const l=document.querySelector('label'),f=document.querySelector('input'),s=document.querySelector('select');const a=new Event('selectstart',{bubbles:true,cancelable:true});l.dispatchEvent(a);const b=new Event('selectstart',{bubbles:true,cancelable:true});f.dispatchEvent(b);const d=new Event('selectstart',{bubbles:true,cancelable:true});s.dispatchEvent(d);const c=new Event('click',{bubbles:true,cancelable:true});s.dispatchEvent(c);return getComputedStyle(l).webkitUserSelect==='none'&&getComputedStyle(f).webkitUserSelect==='text'&&getComputedStyle(l).cursor.includes('data:image/png;base64,')&&getComputedStyle(s).cursor==='default'&&a.defaultPrevented&&!b.defaultPrevented&&!c.defaultPrevented&&!d.defaultPrevented;})()"
        completionHandler:^(id result,NSError *error) {assert(!error && [result boolValue]);done=true;}];
    WaitUntil(^bool {return done;});
}
int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        NSData *png=[[NSData alloc] initWithBase64EncodedString:@"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j9YkAAAAASUVORK5CYII=" options:0];
        ConfigureDesktopWebCursor(png,CGPointZero);
        NSUInteger revision=desktopWebPolicyRevision;
        ConfigureDesktopWebCursor(png,CGPointZero);assert(desktopWebPolicyRevision==revision);
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,800,600)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        WKWebView *web=[[WKWebView alloc] initWithFrame:NSMakeRect(0,0,800,600)];
        [window.contentView addSubview:web];
        unsigned budget=100;assert(FindDesktopWebView(window.contentView,budget,true)==web);
        web.hidden=YES;budget=100;assert(!FindDesktopWebView(window.contentView,budget,true));web.hidden=NO;
        assert(PrepareDesktopWebView(web));assert(!PrepareDesktopWebView(web));
        assert(web.configuration.userContentController.userScripts.count==1);
        InstallDesktopWebHooks();
        WKWebView *registered=[[WKWebView alloc] initWithFrame:NSMakeRect(0,0,100,100)];
        [window.contentView addSubview:registered];registered.hidden=YES;
        assert([desktopWebViews containsObject:registered]);
        assert(!PrepareDesktopWebView(registered));
        DesktopTestNavigation *navigation=[DesktopTestNavigation new];web.navigationDelegate=navigation;
        [web loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"about:blank"]]];
        WaitUntil(^bool{return navigation.done;});assert([desktopWebViews containsObject:web]);
        navigation.done=false;
        NSString *html=@"<html><body><label>Page 2</label><input value='editable'><select><option>one</option><option>two</option></select></body></html>";
        [web loadHTMLString:html baseURL:nil];WaitUntil(^bool{return navigation.done;});CheckDocument(web);
        navigation.done=false;
        [web loadHTMLString:html baseURL:nil];WaitUntil(^bool{return navigation.done;});CheckDocument(web);
        // This offscreen local document never displays a window or sends input
        // to the user's game. Both document loads use the real WebKit engine.
        std::cout<<"WebKit: native view detection, hidden view exclusion, persistent no-selection policy, editable input and dropdown clicks passed\n";
    }
}
