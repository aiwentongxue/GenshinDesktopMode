#include "pointer_geometry.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace DesktopGeometry;
    Rect full{0,0,1920,1080};
    Rect view=FitViewport(full,3456,2160);
    assert(view.x==96 && view.y==0 && view.width==1728 && view.height==1080);
    auto left=Normalize(view,{96,540}),right=Normalize(view,{1824,540});
    assert(left.x==0 && right.x==1 && left.y==.5 && right.y==.5);
    for(auto p:{Point{96,0},Point{1824,0},Point{96,1080},Point{1824,1080},Point{960,540}}) {
        auto n=Normalize(view,p);
        assert(std::fabs(view.x+n.x*view.width-p.x)<1e-9);
        assert(std::fabs(view.y+(1-n.y)*view.height-p.y)<1e-9);
    }
    Rect window=FitViewport({80,60,1728,1080},3456,2160);
    assert(window.x==80 && window.y==60 && window.width==1728);
    Rect external=FitViewport({-1920,200,1920,1080},3456,2160);
    assert(external.x==-1824 && external.y==200);
    assert(AtTopEdge(full,{900,0}) && !AtTopEdge(full,{900,1}) && !AtTopEdge(full,{900,30}));
    assert(!AtTopEdge(full,{-1,0}));
    std::cout<<"Geometry: pillarboxed fullscreen edges, window/display offsets and one-point top edge passed\n";
}
