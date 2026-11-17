#include "../genshin/character_pointer_state.h"
#include "../genshin/character_mouse_units.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    CharacterPointerState s;
    auto frame=[&](bool down,bool available,bool eligible,uint64_t revision,float x,float y) {
        return s.sample(down,available,eligible,revision,x,y,5);
    };
    frame(false,false,false,1,0,0);
    // A small physical movement remains one native 3D click on release.
    auto p=frame(true,true,true,1,100,100);
    assert(p.pressed&&!p.motion.began&&!p.clicked);
    p=frame(true,true,false,1,102,101);
    assert(!p.pressed&&!p.motion.began&&!p.clicked);
    p=frame(false,true,false,1,102,101);
    assert(p.clicked&&!p.motion.ended&&!p.cancelled);
    assert(!frame(false,true,false,1,102,101).clicked);
    // Each eligible press rearms the game's picker after an earlier rotation.
    assert(frame(true,true,true,1,100,100).pressed);
    p=frame(true,true,false,1,106,100);
    assert(p.motion.began&&p.motion.moved&&!p.clicked);
    assert(frame(true,true,false,1,100,100).motion.moved);
    p=frame(false,true,false,1,100,100);
    assert(p.motion.ended&&!p.clicked&&!p.cancelled);
    assert(frame(true,true,true,1,100,100).pressed);
    assert(frame(false,true,false,1,100,100).clicked);
    // Ordinary buttons/list ownership never becomes a model click or drag.
    p=frame(true,true,false,1,100,100);assert(!p.pressed);
    p=frame(true,true,false,1,150,100);assert(!p.motion.began);
    assert(!frame(false,true,false,1,150,100).clicked);
    // Losing focus, leaving the page/window or landing on a widget cancels.
    assert(frame(true,true,true,1,100,100).pressed);
    p=frame(false,true,false,2,100,100);
    assert(p.cancelled&&!p.clicked);
    assert(frame(true,true,true,2,100,100).pressed);
    p=frame(true,false,false,2,100,100);assert(p.cancelled&&!p.clicked);
    assert(!frame(false,true,false,2,100,100).clicked);
    assert(frame(true,true,true,2,100,100).pressed);
    p=frame(false,false,false,2,100,100);assert(p.cancelled&&!p.clicked);
    // Background-held buttons cannot migrate into a foreground click.
    frame(true,false,false,2,100,100);
    assert(!frame(true,true,true,2,100,100).pressed);
    assert(!frame(false,true,false,2,100,100).clicked);
    assert(frame(true,true,true,2,100,100).pressed);
    p=frame(true,true,false,3,110,100);
    assert(p.cancelled&&!p.clicked&&!p.motion.began);
    frame(false,true,false,3,110,100);
    // A disabled scene relinquishes even an armed, not-yet-dragged press.
    assert(frame(true,true,true,3,100,100).pressed);
    assert(s.cancel());assert(!s.cancel());
    assert(!frame(false,true,false,3,100,100).clicked);
    assert(frame(true,true,true,3,100,100).pressed);
    p=frame(false,true,false,3,NAN,100);assert(p.cancelled&&!p.clicked);
    // The physical threshold and rotation units match at 1x/2x rendering.
    for(int render:{1000,2000,3000}) {
        CharacterPointerState scaled;
        scaled.sample(false,false,false,1,0,0,5);
        auto point=[&](float x){return CharacterMouseUnits::fromPixels(x,1000,render,1);};
        scaled.sample(true,true,true,1,point(render*.5f),100,5);
        p=scaled.sample(true,true,false,1,point(render*.504f),100,5);
        assert(!p.motion.began);
        p=scaled.sample(true,true,false,1,point(render*.506f),100,5);
        assert(p.motion.began&&std::fabs(p.motion.x-6)<.001f);
        assert(!scaled.sample(false,true,false,1,point(render*.506f),100,5).clicked);
    }
    std::puts("Character preview: clicks, rotation releases, UI ownership, focus/cancel and render-scale threshold passed");
}
