#pragma once
#include "native_web_visibility.h"

// WebKit paints its own cursor. Keep this presentation-only: no mouse-button
// interception and no changes to the working UnityView cursor renderer.
static NSString *desktopWebCursorRule;
static NSString *desktopWebCursorValue;
static NSData *desktopWebCursorPNG;
static id desktopWebGoldCursor;
static NSUInteger desktopWebStyleRevision=1;
static void (*desktopWebStyleDiagnostic)(bool);
static void ConfigureDesktopWebStyle(NSData *png,CGSize size,CGPoint hotspot) {
    if(!png) png=desktopWebCursorPNG;
    if(!png) return;
    if(CGPointEqualToPoint(hotspot,CGPointZero) && CGSizeEqualToSize(size,CGSizeMake(32,32)))
        hotspot=CGPointMake(2,2);
    NSString *value=[NSString stringWithFormat:@"url(\"data:image/png;base64,%@\") %.0f %.0f,default",
        [png base64EncodedStringWithOptions:0],hotspot.x,hotspot.y];
    NSString *rule=[NSString stringWithFormat:@"cursor:%@!important;",value];
    if([rule isEqualToString:desktopWebCursorRule]) return;
    desktopWebCursorPNG=png;desktopWebCursorRule=rule;desktopWebCursorValue=value;++desktopWebStyleRevision;
    Class imageClass=NSClassFromString(@"NSImage"),cursorClass=NSClassFromString(@"NSCursor");
    if(!imageClass || !cursorClass) return;
    id image=((id (*)(id,SEL))objc_msgSend)(imageClass,sel_registerName("alloc"));
    image=((id (*)(id,SEL,id))objc_msgSend)(image,sel_registerName("initWithData:"),png);
    if(!image) return;
    ((void (*)(id,SEL,CGSize))objc_msgSend)(image,sel_registerName("setSize:"),size);
    id cursor=((id (*)(id,SEL))objc_msgSend)(cursorClass,sel_registerName("alloc"));
    desktopWebGoldCursor=((id (*)(id,SEL,id,CGPoint))objc_msgSend)(cursor,
        sel_registerName("initWithImage:hotSpot:"),image,hotspot);
}
static NSString *DesktopWebStyleScript(void) {
    NSString *source=@"(()=>{const apply=w=>{try{const d=w.document;"
        "if(!d.documentElement)return;"
        "let s=d.getElementById('__gdm_web_style_r31');"
        "if(!s){s=d.createElement('style');s.id='__gdm_web_style_r31';d.documentElement.appendChild(s);}"
        "const css='html,body,body *{user-select:none!important;-webkit-user-select:none!important}input,textarea,[contenteditable=true],[contenteditable=true] *{user-select:text!important;-webkit-user-select:text!important}select,option{user-select:auto!important;-webkit-user-select:auto!important}:is(#__gdm_gold_a#__gdm_gold_b,html),:is(#__gdm_gold_a#__gdm_gold_b,html) *{__GDM_CURSOR_RULE__}';"
        "if(s.textContent!==css)s.textContent=css;"
        "const edit=t=>t&&t.closest&&t.closest('input,textarea,select,option,[role=combobox],[contenteditable=true]');"
        "if(!edit(d.activeElement)){const q=w.getSelection();if(q)q.removeAllRanges();}"
        "if(!w.__gdmWebStyle_r31){w.__gdmWebStyle_r31=true;d.addEventListener('selectstart',e=>{if(!edit(e.target))e.preventDefault();},true);}"
        "if(!w.__gdmWebRepair_r36){w.__gdmWebRepair_r36=true;"
        "let queued=false;new w.MutationObserver(()=>{if(queued)return;queued=true;Promise.resolve().then(()=>{queued=false;apply(w);});}).observe(d,{childList:true,subtree:true});"
        "d.addEventListener('pointerover',e=>{const t=e.target;if(t&&t.style&&!w.getComputedStyle(t).cursor.includes('data:image/png;base64,'))t.style.setProperty('cursor','__GDM_CURSOR_VALUE__','important');},true);}"
        "for(let i=0;i<w.frames.length;i++)apply(w.frames[i]);}catch(e){}};"
        "apply(window);if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',()=>apply(window),{once:true});return !!document.documentElement&&getComputedStyle(document.documentElement).cursor.includes('data:image/png;base64,');})()";
    source=[source stringByReplacingOccurrencesOfString:@"__GDM_CURSOR_RULE__" withString:desktopWebCursorRule?:@""];
    return [source stringByReplacingOccurrencesOfString:@"__GDM_CURSOR_VALUE__" withString:desktopWebCursorValue?:@"default"];
}
static bool PrepareDesktopWebStyle(id web,bool refresh=false) {
    static char policyKey;
    if(!desktopWebCursorRule) return false;
    NSNumber *previous=objc_getAssociatedObject(web,&policyKey);
    bool installed=previous.unsignedIntegerValue==desktopWebStyleRevision;
    if(installed && !refresh) return false;
    NSString *source=DesktopWebStyleScript();
    Class scriptClass=NSClassFromString(@"WKUserScript");
    if(!installed && scriptClass && [web respondsToSelector:sel_registerName("configuration")]) {
        id configuration=((id (*)(id,SEL))objc_msgSend)(web,sel_registerName("configuration"));
        id controller=((id (*)(id,SEL))objc_msgSend)(configuration,sel_registerName("userContentController"));
        id script=((id (*)(id,SEL))objc_msgSend)(scriptClass,sel_registerName("alloc"));
        script=((id (*)(id,SEL,id,NSInteger,BOOL))objc_msgSend)(script,
            sel_registerName("initWithSource:injectionTime:forMainFrameOnly:"),source,1,NO);
        if(controller && script) ((void (*)(id,SEL,id))objc_msgSend)(controller,sel_registerName("addUserScript:"),script);
    }
    void (^completed)(id,NSError *)=^(id value,NSError *error) {
        if(desktopWebStyleDiagnostic) desktopWebStyleDiagnostic(!error && [value respondsToSelector:@selector(boolValue)] && [value boolValue]);
    };
    ((void (*)(id,SEL,id,id))objc_msgSend)(web,sel_registerName("evaluateJavaScript:completionHandler:"),source,completed);
    objc_setAssociatedObject(web,&policyKey,@(desktopWebStyleRevision),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return true;
}

// Register the existing all-frame style before an SDK starts navigation. This
// covers cross-origin child documents without reading their contents.
static id (*originalDesktopStyleRequest)(id,SEL,id);
static id (*originalDesktopStyleHTML)(id,SEL,id,id);
static id (*originalDesktopStyleFile)(id,SEL,id,id);
static id LoadDesktopStyledRequest(id web,SEL selector,id request) {
    PrepareDesktopWebStyle(web);return originalDesktopStyleRequest(web,selector,request);
}
static id LoadDesktopStyledHTML(id web,SEL selector,id html,id baseURL) {
    PrepareDesktopWebStyle(web);return originalDesktopStyleHTML(web,selector,html,baseURL);
}
static id LoadDesktopStyledFile(id web,SEL selector,id URL,id accessURL) {
    PrepareDesktopWebStyle(web);return originalDesktopStyleFile(web,selector,URL,accessURL);
}
static bool InstallDesktopWebStyleHooks(void) {
    Class cls=NSClassFromString(@"WKWebView");if(!cls) return false;
    if(!originalDesktopStyleRequest) {
        SEL selector=sel_registerName("loadRequest:");Method method=class_getInstanceMethod(cls,selector);
        if(method) {originalDesktopStyleRequest=(id (*)(id,SEL,id))method_getImplementation(method);
            class_replaceMethod(cls,selector,(IMP)LoadDesktopStyledRequest,method_getTypeEncoding(method));}
    }
    if(!originalDesktopStyleHTML) {
        SEL selector=sel_registerName("loadHTMLString:baseURL:");Method method=class_getInstanceMethod(cls,selector);
        if(method) {originalDesktopStyleHTML=(id (*)(id,SEL,id,id))method_getImplementation(method);
            class_replaceMethod(cls,selector,(IMP)LoadDesktopStyledHTML,method_getTypeEncoding(method));}
    }
    if(!originalDesktopStyleFile) {
        SEL selector=sel_registerName("loadFileURL:allowingReadAccessToURL:");Method method=class_getInstanceMethod(cls,selector);
        if(method) {originalDesktopStyleFile=(id (*)(id,SEL,id,id))method_getImplementation(method);
            class_replaceMethod(cls,selector,(IMP)LoadDesktopStyledFile,method_getTypeEncoding(method));}
    }
    return originalDesktopStyleRequest || originalDesktopStyleHTML || originalDesktopStyleFile;
}
