#pragma once

// AppKit supplies system-scaled deltas. A programmatic cursor warp can be
// included in the next delta even though the warp posts no movement event.
// All positions here use desktop points with a top-left origin.
struct DesktopMotion {
    struct Point {double x,y;};
    Point previous{},lastWarp{};
    bool warped=false;
    void reset(Point location) {previous=location;warped=false;}
    void didWarp(Point location) {lastWarp=location;warped=true;}
    Point consume(Point delta,Point currentLocation) {
        if(warped) {
            delta.x+=previous.x-lastWarp.x;
            delta.y+=previous.y-lastWarp.y;
        }
        previous=currentLocation;warped=false;
        return delta;
    }
};
