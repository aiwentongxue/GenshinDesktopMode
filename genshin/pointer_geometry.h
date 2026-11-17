#pragma once
#include <algorithm>
#include <cmath>

namespace DesktopGeometry {
struct Rect {double x,y,width,height;};
struct Point {double x,y;};
static Rect FitViewport(Rect container,double renderWidth,double renderHeight) {
    if(!(container.width>0 && container.height>0 && renderWidth>0 && renderHeight>0)) return container;
    double scale=std::min(container.width/renderWidth,container.height/renderHeight);
    double width=renderWidth*scale,height=renderHeight*scale;
    return {container.x+(container.width-width)/2,container.y+(container.height-height)/2,width,height};
}
static Point Normalize(Rect viewport,Point point) {
    return {(point.x-viewport.x)/viewport.width,1-(point.y-viewport.y)/viewport.height};
}
static bool AtTopEdge(Rect screen,Point point) {
    return point.x>=screen.x && point.x<screen.x+screen.width &&
        point.y>=screen.y && point.y<screen.y+1;
}
}
