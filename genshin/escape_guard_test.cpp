#include "escape_guard.h"
#include "keyboard_state.h"
#include <cassert>
#include <cstdio>

int main() {
    DesktopEscapeGuard guard;
    KeyboardState keyboard;
    keyboard.focus(true);keyboard.reconcile({});keyboard.pending.clear();
    auto route=[&](unsigned type,unsigned code,uint64_t flags,bool game,bool active) {
        auto d=guard.event(type,code,flags,game,active);
        if(d.consume) keyboard.event(27,d.pressed);
        return d.consume;
    };
    // Fullscreen and windowed game windows deliberately use the same route.
    for(bool fullscreen:{false,true}) {
        (void)fullscreen;guard.reset();keyboard.pending.clear();
        assert(route(10,53,0,true,true));
        assert(route(10,53,0,true,true)); // repeat is still consumed
        assert(route(11,53,0,true,true));
        assert(keyboard.pending.size()==2);
        assert(keyboard.pending[0]==std::make_pair(27,true));
        assert(keyboard.pending[1]==std::make_pair(27,false));
    }
    assert(!route(10,53,0,false,true)); // unrelated/native window passes
    assert(!route(10,53,1ULL<<20,true,true)); // Command shortcut passes
    assert(!route(10,48,0,true,true)); // Tab passes
    assert(!route(10,53,0,true,false)); // inactive app passes
    assert(route(10,53,0,true,true));
    assert(route(11,53,0,false,true)); // paired release after a mode change
    assert(route(10,53,0,true,true));
    guard.reset();keyboard.focus(false);
    assert(!guard.ownsPress && !keyboard.held[27]);
    puts("Windowed/fullscreen game Escape consumes both edges; shortcuts, native windows and focus reset preserved.");
}
