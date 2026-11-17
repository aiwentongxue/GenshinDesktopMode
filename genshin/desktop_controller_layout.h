#pragma once
#include "controller_ui_fingerprints.h"

// The native controller view uses the PS layout family on desktop as well.
// iOS adds a separate MobileJoystick override to that family. Suppress only
// this UI predicate for the controller view family; keep the real platform,
// device reader, controller bindings and keyboard views intact. Native UI
// rebuilds can precede the cached input-mode update, so that cache must not
// determine which canvas and asset family the rebuild chooses.
namespace DesktopControllerLayout {
static bool (*originalMobileJoystickLayout)(int,void *);
static std::atomic<bool> controllerViewObserved{false};

static bool MobileJoystickLayout(int layout,void *method) {
    if(layout==2) {
        if(!controllerViewObserved.exchange(true))
            Log(@"desktop_controller_layout applied=1 requested_layout=2 cached_input=%d state_independent=1 profile=%s",NativeBridge::controllerInputActive.load(),RuntimeProfileName());
        return false;
    }
    return originalMobileJoystickLayout(layout,method);
}

static bool Ready(void) {return originalMobileJoystickLayout!=nullptr;}
static int Select(int inputMode,int requestedLayout) {
    if(inputMode==2) return 1;
    if(inputMode==3) return Ready()?2:1;
    return requestedLayout;
}

static void Install(void) {
    if(Ready() || !IsCompatibleRuntime() || !IsModernRuntime()) return;
    const bool global=IsGlobalModernRuntime();
    uintptr_t address=global?ControllerMobileGateGlobalAddress:ControllerMobileGateCNAddress;
    const uint8_t *expected=global?ControllerMobileGateGlobalExpected:ControllerMobileGateCNExpected;
    const uint8_t *target=RuntimeImageRange(address,32,false);
    if(!target || memcmp(target,expected,32)) {
        Log(@"desktop_controller_layout refused_unexpected_ui_predicate profile=%s",RuntimeProfileName());
        return;
    }
    // Both verified implementations start with three stack-register pairs and
    // a frame-pointer add. There are no PC-relative instructions to relocate.
    const uint32_t prologue[]={0xa9bd57f6,0xa9014ff4,0xa9027bfd,0x910083fd};
    if(memcmp(target,prologue,sizeof(prologue))) return;
    void *trampoline=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(trampoline==MAP_FAILED) return;
    memcpy(trampoline,target,16);
    NativeBridge::AbsoluteJump((char *)trampoline+16,(uintptr_t)target+16);
    sys_icache_invalidate(trampoline,32);
    if(mprotect(trampoline,4096,PROT_READ|PROT_EXEC)) {munmap(trampoline,4096);return;}
    uint8_t jump[16];NativeBridge::AbsoluteJump(jump,(uintptr_t)MobileJoystickLayout);
    originalMobileJoystickLayout=(bool (*)(int,void *))trampoline;
    if(!NativeBridge::PatchCode((void *)target,jump,16)) {
        originalMobileJoystickLayout=nullptr;munmap(trampoline,4096);return;
    }
    Log(@"desktop_controller_layout installed=1 family=PS mobile_ui_override=0 state_independent=1 platform_unchanged=1 profile=%s",RuntimeProfileName());
}
}
