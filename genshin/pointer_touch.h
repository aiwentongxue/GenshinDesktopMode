#pragma once
#include <unordered_set>

// Position history belongs to the physical drag, not to UIKit's alternating
// original and translated event objects. Repeated delivery at one position
// produces zero movement rather than jumping back to the press location.
struct DesktopTouchHistory {
    struct Point {double x=0,y=0;};
    const void *identity=nullptr;
    Point current{},previous{};
    void begin(const void *touch,Point point) {identity=touch;current=previous=point;}
    bool update(const void *touch,Point point) {
        if(!identity || identity!=touch) return false;
        previous=current;current=point;return true;
    }
    void reset() {identity=nullptr;current=previous={};}
};

// Keep a native-owned UIKit stream suppressed until its terminal phase, even
// if the map closes or focus changes. A later Began resets a recycled identity.
class DesktopTouchStreamOwnership {
    std::unordered_set<const void *> native;
public:
    bool suppress(const void *identity,int phase,bool claimBegan) {
        if(!identity) return false;
        if(phase==0) {
            if(claimBegan) native.insert(identity);
            else native.erase(identity);
        }
        bool owned=native.count(identity)!=0;
        if(phase==3 || phase==4) native.erase(identity);
        return owned;
    }
};
