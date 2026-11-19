#include "primary_button.h"
#include "map_mouse_gesture.h"
#include "map_projection.h"
#include "map_pointer_frame.h"
#include "pointer_touch.h"
#include <cassert>

int main() {
    DesktopPrimaryButton button;
    DesktopPrimaryButton::Point p{.25f,.75f,true}, q{.26f,.73f,true};
    // Two complete taps between engine ticks, including duplicate delivery.
    for(int i=0;i<2;++i) {
        button.event(true,true,p); button.event(true,true,p);
        button.event(false,true,q); button.event(false,true,q);
    }
    for(int i=0;i<2;++i) {
        auto press=button.frame(), release=button.frame();
        assert(press.down && press.position.x==p.x && !release.down && release.position.y==q.y);
    }
    assert(!button.frame().down);
    button.event(true,false,p); button.event(false,true,q); // window chrome
    assert(!button.frame().down);
    button.event(true,true,p); assert(button.frame().down && button.frame().down);
    // Switch before the up packet; a fresh tap after returning is retained.
    button.cancel(); button.event(false,true,q); button.cancel();
    button.event(true,true,p); button.event(false,true,q);
    assert(!button.frame().down && button.frame().down && !button.frame().down);
    // A boundary between receipt and consumption drops the whole old click.
    button.event(true,true,p); button.event(false,true,q); button.cancel();
    assert(!button.frame().down && !button.frame().down);
    for(int i=0;i<5;++i) {button.event(true,true,p);button.event(false,true,q);}
    for(int i=0;i<10;++i) assert(!button.frame().down);
    // Stale hardware state cannot create a press. A missing physical up
    // ends an observed hold once and a later AppKit up cannot replay it.
    assert(!button.frame(true).down);
    button.event(true,true,p);assert(button.frame(true).down);
    assert(button.frame(false).down && !button.frame(false).down);
    button.event(false,true,q);assert(!button.frame(true).down);

    DesktopTouchStreamOwnership streams;
    int a,b;
    assert(!streams.suppress(&a,0,false));
    assert(streams.suppress(&b,0,true));
    assert(!streams.suppress(&a,3,true));   // previously forwarded Ended
    assert(streams.suppress(&b,4,false));  // previously suppressed Cancelled
    assert(!streams.suppress(&b,0,false)); // same touch identity reused

    for(int scale:{1,2,3}) {
        DesktopMapMouseGesture map;
        assert(map.viewport.capture(1000*scale,700*scale,1000,700));
        auto sample=[&](bool down,float x,float y,bool eligible=true,unsigned focus=3) {
            return map.sample(down,true,eligible,focus,x*scale,y*scale);
        };
        sample(true,40,50);
        assert(sample(false,42,51).click);
        sample(true,40,50);
        auto begin=sample(true,44,50);
        assert(begin.begin && begin.dx==4*scale);
        auto move=sample(true,50,61);
        assert(!move.begin && move.dx==6*scale && move.dy==11*scale);
        auto end=sample(false,53,61);
        assert(end.end && !end.click && end.dx==3*scale);
        sample(true,40,50);                 // fast flick, no intermediate frame
        auto flick=sample(false,29,50);
        assert(flick.begin && flick.end && !flick.click && flick.dx==-11*scale);
        sample(true,40,50); sample(true,50,50);
        assert(!sample(false,40,50).click); // return to original point
        sample(true,40,50,false);
        assert(!sample(false,40,50).click);
        sample(true,40,50);
        assert(!sample(false,40,50,true,4).click);
        sample(true,40,50);
        map.sample(true,false,false,3,40*scale,50*scale);
        assert(!sample(false,40,50).click);
    }

    using namespace DesktopMapProjection;
    // Perspective camera with an offset viewport, camera tilt and zoom.
    // Drag the very same point across the view and back; compare its
    // projected final position against the desired display pixels.
    for(float depth:{8.f,22.f,55.f}) for(int width:{1600,3840,5120}) {
        Viewport v{{.12f,.04f,.81f,.92f},float(width),width*9.f/16};
        Vector3 camera{11,28,-depth}, anchor{};
        auto ray=[&](float x,float y,float z) {
            auto u=v.fromPixel(x,y,z);
            return Vector3{camera.x+(u.x-.5f)*z,camera.y+(u.y-.5f+.12f)*z,camera.z+z};
        };
        auto project=[&](Vector3 point) {
            float z=point.z-camera.z;
            return v.toPixel({.5f+(point.x-camera.x)/z,.5f-.12f+(point.y-camera.y)/z,z});
        };
        float x0=.38f*v.width,y0=.64f*v.height;
        assert(intersect(ray(x0,y0,depth*.5f),ray(x0,y0,depth),{0,0,0},{.1f,.2f,1},anchor));
        for(float t:{.1f,.75f,1.f,.31f,0.f}) {
            float x=x0+150*t,y=y0-78*t;
            Vector3 under{};
            assert(intersect(ray(x,y,depth*.5f),ray(x,y,depth),anchor,{0,0,1},under));
            camera.x+=anchor.x-under.x; camera.y+=anchor.y-under.y;
            auto seen=project(anchor);
            assert(std::hypot(seen.x-x,seen.y-y)<.01f && camera.z==-depth);
        }
    }
    Vector3 unused{};
    assert(!intersect({0,0,1},{1,0,1},{},{0,0,1},unused));
    FlatMap flat{{{0,.1f,1,.8f},3840,2160},2600,1170};
    for(float zoom:{.5f,1.f,2.f}) {
        auto f=flat; f.worldWidth*=zoom;f.worldHeight*=zoom;
        Vector3 pivot{250,-80,-600};
        auto held=f.at(1280,900,pivot),next=f.at(1632,645,pivot);
        pivot.x+=held.x-next.x;pivot.y+=held.y-next.y;
        auto seen=f.pixels(held,pivot);
        assert(std::hypot(seen.x-1632,seen.y-645)<.001f);
    }
    DesktopMapPointerFrame cursor;
    double x,y;
    assert(cursor.publish(10,20,900,600,1)&&!cursor.drawPoint(x,y));
    auto g=cursor.snapshot();
    assert(cursor.record(g,1,123,456)&&cursor.drawPoint(x,y)&&x==123&&y==456);
    assert(cursor.publish(10,20,920,600,1)&&!cursor.drawPoint(x,y));
    assert(!cursor.record(g,1,12,34));
    cursor.cancel();assert(!cursor.drawPoint(x,y));
}
