#pragma once
#include <atomic>

namespace DesktopControlPreference {
static std::atomic<bool> startupRestored{false};
static std::atomic<bool> bootstrapReady{false};
// Native controller discovery also calls the mode switch directly. Keep all
// such routes on keyboard during login, not just our saved-mode restoration.
static std::atomic<bool> gameplayReady{false};
static void (*explicitSelection)(int);
static NSString *const key=@"GenshinDesktopPreferredControlDevice";
static int SavedMode(void) {
    id value=[NSUserDefaults.standardUserDefaults objectForKey:key];
    int mode=[value isKindOfClass:NSNumber.class]?[value intValue]:2;
    return mode==3?3:2;
}
static bool HasController(void) {
    for(GCController *controller in GCController.controllers)
        if(controller.extendedGamepad)return true;
    return false;
}
static int LaunchMode(void) {
    return SavedMode()==3&&HasController()?3:2;
}
static void RememberSelection(int requested,int actual,const char *source="control_device_menu") {
    if((requested!=2&&requested!=3)||actual!=requested)return;
    if(SavedMode()!=actual || ![NSUserDefaults.standardUserDefaults objectForKey:key])
        [NSUserDefaults.standardUserDefaults setInteger:actual forKey:key];
    startupRestored=true;
    Log(@"control_preference saved=%d source=%s",actual,source);
}
}
