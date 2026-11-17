#include "../genshin/codex_pointer_state.h"
#include "../genshin/character_mouse_units.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    CodexPointerState state;
    auto frame=[&](bool held,bool available,bool eligible,uint64_t focus,float x,float y=100) {
        return state.sample(held,available,eligible,focus,x,y,5);
    };
    frame(false,false,false,1,100);
    // A short press must never call native begin/end: native end raycasts.
    auto s=frame(true,true,true,1,100);
    assert(!s.begin&&!s.end&&!s.click);
    s=frame(true,true,false,1,103);assert(!s.begin&&!s.update);
    s=frame(false,true,false,1,103);assert(s.click&&!s.begin&&!s.end);
    assert(!frame(false,true,false,1,103).click);
    // Physical drag begins exactly once and updates every held frame. A
    // stationary frame is zero, rather than a replay of the previous delta.
    frame(true,true,true,1,100);
    s=frame(true,true,false,1,106);
    assert(s.begin&&s.update&&s.deltaX==6&&!s.click);
    s=frame(true,true,false,1,110);assert(!s.begin&&s.update&&s.deltaX==4);
    s=frame(true,true,false,1,110);assert(s.update&&s.deltaX==0);
    s=frame(true,true,false,1,100);assert(s.update&&s.deltaX==-10);
    s=frame(false,true,false,1,100);assert(s.end&&!s.update&&!s.click);
    assert(!frame(false,true,false,1,100).end);
    // Armed clicks cancel without any native release/click on focus loss.
    frame(true,true,true,1,100);
    s=frame(false,true,false,2,100);assert(!s.click&&!s.end);
    // Cancelling a real drag does send one native end, never a click.
    frame(true,true,true,2,100);assert(frame(true,true,false,2,110).begin);
    s=frame(true,false,false,2,120);assert(s.end&&!s.click&&!s.update);
    assert(!frame(true,true,true,2,130).begin);
    assert(!frame(false,true,false,2,130).click);
    // UI-owned presses cannot migrate to the carousel while still held.
    frame(true,true,false,2,100);
    s=frame(true,true,true,2,130);assert(!s.begin&&!s.update);
    assert(!frame(false,true,false,2,130).click);
    // Scene disable/resize/page-close: end only a drag actually begun.
    frame(true,true,true,2,100);assert(!state.cancel());
    assert(!frame(false,true,false,2,100).click);
    frame(true,true,true,2,100);frame(true,true,false,2,106);
    assert(state.cancel());assert(!state.cancel());
    assert(!frame(false,true,false,2,100).click);
    frame(true,true,true,2,100);frame(true,true,false,2,106);
    s=frame(false,true,false,3,100);assert(s.end&&!s.click);
    frame(true,true,true,3,100);frame(true,true,false,3,106);
    s=frame(true,true,false,3,NAN);assert(s.end&&!s.update&&!s.click);
    frame(false,false,false,3,100);
    // Threshold and original Mouse X axis units are render-scale independent.
    for(int render:{1000,2000,3000}) {
        CodexPointerState scaled;
        auto point=[&](float x){return CharacterMouseUnits::fromPixels(x,1000,render,1);};
        scaled.sample(false,false,false,1,0,0,5);
        scaled.sample(true,true,true,1,point(render*.5f),100,5);
        s=scaled.sample(true,true,false,1,point(render*.504f),100,5);
        assert(!s.begin);
        s=scaled.sample(true,true,false,1,point(render*.506f),100,5);
        assert(s.begin&&s.update&&std::fabs(s.deltaX*.1f-.6f)<.0001f);
        s=scaled.sample(false,true,false,1,point(render*.506f),100,5);
        assert(s.end&&!s.click);
    }
    std::puts("Codex mouse: click/drag separation, native release, focus/page cancellation and scale units passed");
}
