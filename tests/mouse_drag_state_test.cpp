#include "../genshin/mouse_drag_state.h"
#include <cassert>
#include <limits>

int main() {
    DesktopMouseDragState s;
    s.sample(false,true,true,1,100,100);
    assert(!s.sample(true,true,true,1,100,100).began);
    assert(!s.sample(true,true,true,1,103,104).moved);
    auto first=s.sample(true,true,true,1,112,109);
    assert(first.began&&first.moved&&first.x==12&&first.y==9);
    auto next=s.sample(true,true,true,1,118,105);
    assert(!next.began&&next.moved&&next.x==6&&next.y==-4);
    assert(!s.sample(true,true,true,1,118,105).moved);
    assert(s.sample(false,true,true,1,118,105).ended);
    assert(!s.sample(false,true,true,1,118,105).ended);

    // A press on a widget or outside the game cannot become a map drag
    // simply by crossing into its content while the button stays held.
    s.sample(true,true,false,1,0,0);
    assert(!s.sample(true,true,true,1,100,100).moved);
    s.sample(false,true,true,1,100,100);
    s.sample(true,false,true,1,0,0);
    assert(!s.sample(true,true,true,1,100,100).began);
    s.sample(false,true,true,1,100,100);

    s.sample(true,true,true,1,0,0);
    assert(s.sample(true,true,true,1,20,0).began);
    assert(s.sample(true,true,true,2,30,0).ended);
    assert(!s.sample(true,true,true,2,100,0).moved);
    s.sample(false,true,true,2,100,0);
    s.sample(true,true,true,2,100,0);
    assert(s.sample(true,true,true,2,80,0).began);
    assert(s.cancel().ended);
    assert(!s.sample(true,true,true,2,10,0).began);
}
