#pragma once
#include <cmath>

namespace DesktopMapProjection {
struct Vector3 {float x=0,y=0,z=0;};
struct Rect {float x,y,width,height;};
struct Viewport {
    Rect rect{};
    float width=0,height=0;
    bool valid() const {
        return std::isfinite(rect.x)&&std::isfinite(rect.y)&&std::isfinite(rect.width)&&
            std::isfinite(rect.height)&&rect.width>0&&rect.height>0&&width>0&&height>0;
    }
    Vector3 fromPixel(float x,float y,float depth) const {
        return {(x/width-rect.x)/rect.width,(y/height-rect.y)/rect.height,depth};
    }
    Vector3 toPixel(Vector3 p) const {
        return {(rect.x+p.x*rect.width)*width,(rect.y+p.y*rect.height)*height,p.z};
    }
};
// A flat map can also expose its exact visible world width and height
// through the camera's frustum, even when it has no projected UI panel.
struct FlatMap {
    Viewport view;
    float worldWidth,worldHeight;
    bool valid() const {
        return view.valid()&&std::isfinite(worldWidth)&&std::isfinite(worldHeight)&&worldWidth>0&&worldHeight>0;
    }
    Vector3 at(float x,float y,Vector3 camera) const {
        auto v=view.fromPixel(x,y,0);
        return {camera.x+(v.x-.5f)*worldWidth,camera.y+(v.y-.5f)*worldHeight,0};
    }
    Vector3 pixels(Vector3 point,Vector3 camera) const {
        return view.toPixel({.5f+(point.x-camera.x)/worldWidth,.5f+(point.y-camera.y)/worldHeight,0});
    }
};
inline bool finite(Vector3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
// The two points lie on the same screen ray at different camera depths.
// This also works for orthographic and tilted cameras.
inline bool intersect(Vector3 a,Vector3 b,Vector3 plane,Vector3 normal,Vector3 &hit) {
    if(!finite(a)||!finite(b)||!finite(plane)||!finite(normal)) return false;
    double dx=double(b.x)-a.x,dy=double(b.y)-a.y,dz=double(b.z)-a.z;
    double denom=dx*normal.x+dy*normal.y+dz*normal.z;
    if(std::fabs(denom)<1e-7) return false;
    double t=((double(plane.x)-a.x)*normal.x+(double(plane.y)-a.y)*normal.y+
              (double(plane.z)-a.z)*normal.z)/denom;
    hit={float(a.x+t*dx),float(a.y+t*dy),float(a.z+t*dz)};
    return finite(hit);
}
}
