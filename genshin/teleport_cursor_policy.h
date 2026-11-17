#pragma once
#include <cstdint>

// A map-to-loading transition owns cursor visibility until gameplay takes it
// back. Map selection/cancellation and ordinary loading never arm this latch.
struct TeleportCursorPolicy {
    bool hidden=false,seenMap=false,previousLoading=false,settling=false;
    double lastMapTime=0,loadingEndedTime=0;
    uint64_t showRevisionAtLoadingEnd=0;

    bool sample(bool mapOpen,bool loading,bool requestedVisible,uint64_t showRevision,
                bool explicitCursorUI,double now) {
        bool before=hidden;
        if(mapOpen && !loading) {seenMap=true;lastMapTime=now;hidden=false;settling=false;}
        if(loading && !previousLoading && seenMap && now-lastMapTime<=3.0) {
            hidden=true;settling=false;
        }
        if(hidden && !loading && previousLoading) {
            settling=true;loadingEndedTime=now;showRevisionAtLoadingEnd=showRevision;
        }
        if(hidden && !loading) {
            if(!requestedVisible || mapOpen || explicitCursorUI ||
               (settling && showRevision!=showRevisionAtLoadingEnd) ||
               (settling && now-loadingEndedTime>=2.0)) {
                hidden=false;settling=false;
            }
        }
        previousLoading=loading;
        return before!=hidden;
    }
};
