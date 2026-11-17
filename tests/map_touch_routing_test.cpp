#include "../genshin/pointer_touch.h"
#include <cassert>
#include <iostream>

int main() {
    DesktopTouchStreamOwnership routes;
    int mapTouch=0,otherTouch=0;
    assert(routes.suppress(&mapTouch,0,true));
    assert(routes.suppress(&mapTouch,1,false)); // map has closed: still native-owned
    assert(!routes.suppress(&otherTouch,0,false)); // unrelated UIKit stream passes
    assert(!routes.suppress(&otherTouch,1,false));
    assert(routes.suppress(&mapTouch,3,false)); // swallow its matching Ended
    assert(!routes.suppress(&otherTouch,3,false));
    assert(!routes.suppress(&mapTouch,0,false)); // recycled identity in normal UI
    assert(!routes.suppress(&mapTouch,1,false));
    assert(!routes.suppress(&mapTouch,3,false));
    assert(routes.suppress(&mapTouch,0,true));
    assert(routes.suppress(&mapTouch,4,false)); // focus cancellation is terminal
    assert(!routes.suppress(&mapTouch,0,false));
    assert(!routes.suppress(nullptr,0,true));
    std::cout<<"Map touch ownership survives menu/focus changes; terminal phases and unrelated streams route correctly\n";
}
