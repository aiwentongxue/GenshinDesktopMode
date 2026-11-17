#pragma once
#include <cstring>

namespace GenshinIdentity {
inline bool Namespace(const char *value,const char *prefix) {
    if(!value) return false;
    size_t n=strlen(prefix);
    return !strncmp(value,prefix,n) && (value[n]=='\0' || value[n]=='.');
}
inline bool IsGame(const char *bundle,const char *executable=nullptr) {
    // Identity is exclusively the bundle namespace. Executable names, game
    // version strings and signing UUIDs must not enable or disable the plugin.
    (void)executable;
    return Namespace(bundle,"com.miHoYo.Yuanshen") || Namespace(bundle,"com.miHoYo.GenshinImpact") ||
        Namespace(bundle,"com.HoYoverse.GenshinImpact");
}
}
