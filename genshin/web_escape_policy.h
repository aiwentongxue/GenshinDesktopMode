#pragma once
#include <cstring>
#include <initializer_list>

// Match the game's wish-record route, never arbitrary account/login pages.
// Callers pass URL.path only: query strings and credentials are not examined.
static bool IsDesktopWishHistoryPath(const char *path) {
    if(!path) return false;
    for(const char *route:{"/gacha/","/gacha-","/gacha_","/gachalog/"})
        if(strstr(path,route)) return true;
    // Older and current game-web deployments may prefix the route with an
    // event date, e.g. e20190909gacha-v3. Treat that as the same page family.
    for(const char *segment=path;(segment=strstr(segment,"/e"));++segment) {
        const char *p=segment+2;
        while(*p>='0' && *p<='9') ++p;
        if(p-segment>=8 && !strncmp(p,"gacha",5) &&
           (p[5]=='/' || p[5]=='-' || p[5]=='_')) return true;
    }
    return false;
}
