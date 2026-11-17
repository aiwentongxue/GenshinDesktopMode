#pragma once

namespace DesktopControlDevicePolicy {
static constexpr int StartupInput(int requested,bool bootstrapped,bool gameplay) {
    return requested==3&&(!bootstrapped||!gameplay)?2:requested;
}
// The verified native selector supplies touch at 0 and an available controller
// at 1. Replace slot 0 with keyboard/mouse, keeping the controller's real index.
static constexpr bool KnownOptions(int nativeCount) {return nativeCount==1||nativeCount==2;}
static constexpr int SelectedIndex(int nativeCount,int inputMode) {
    return KnownOptions(nativeCount)?(inputMode==3&&nativeCount==2?1:0):-1;
}
static constexpr int InputMode(int nativeCount,int visibleIndex) {
    return !KnownOptions(nativeCount)?-1:visibleIndex==0?2:
        visibleIndex==1&&nativeCount==2?3:-1;
}
static constexpr int DesktopFallback(int previous,int requested) {
    // A disconnected controller must return to keyboard/mouse, since touch
    // is no longer offered. Leave initial login/loading mode changes alone.
    return previous==3&&requested==0?2:requested;
}
static constexpr bool NeedsDesktopRebuild(int previous,int current,bool insideLayoutSwitch) {
    return !insideLayoutSwitch&&previous!=current&&(current==2||current==3);
}
}
