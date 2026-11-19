#pragma once

// These SDK sheets share the Unity window on Catalyst. UIKit dispatches
// the real event to the sheet; the desktop bridge must not give a second
// copy to the game's full-screen title button behind it.
namespace NativeAccountInput {
static bool Visible() {
    static __weak UIView *unityView;
    if (!unityView.window) {
        for (UIWindow *window in UIApplication.sharedApplication.windows)
            if (!window.hidden && (unityView=FindUnityView(window))) break;
    }
    if (!unityView || unityView.hidden || unityView.window.hidden) return false;
    static Class panel=NSClassFromString(@"MiHoYoBGView");
    static Class alert=NSClassFromString(@"MiHoYoAlertView");
    static Class loading=NSClassFromString(@"MiHoYoLoadingView");
    for (UIView *view in unityView.subviews) {
        if (view.hidden || view.alpha<.01) continue;
        if ([view isKindOfClass:panel] || [view isKindOfClass:alert] || [view isKindOfClass:loading])
            return true;
    }
    return false;
}

static bool Refresh(bool primaryDown=false) {
    static unsigned clicks;
    bool visible=Visible();
    if (visible && primaryDown) ++clicks;
    if (visible!=NativeBridge::accountSheetActive.load()) {
        NativeBridge::SetAccountSheetActive(visible);
        Log(@"account_input sheet=%d sheet_clicks=%u",visible,clicks);
        if (!visible) clicks=0;
    }
    return visible;
}
}
