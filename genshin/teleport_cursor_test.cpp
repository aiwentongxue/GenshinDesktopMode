#include "teleport_cursor_policy.h"
#include <cassert>
#include <cstdio>

int main() {
    TeleportCursorPolicy policy;
    // Ordinary gameplay, startup loading and unrelated scenes do not hide UI.
    assert(!policy.sample(false,true,true,1,false,0));
    assert(!policy.hidden);
    policy.sample(false,false,false,1,false,1);
    policy.sample(true,false,true,2,false,2);assert(!policy.hidden);
    policy.sample(true,false,true,3,false,4);assert(!policy.hidden); // mark/detail selection
    policy.sample(false,false,false,3,false,5);assert(!policy.hidden); // cancel map
    policy.sample(false,true,true,3,false,9);assert(!policy.hidden); // unrelated later load
    policy.sample(false,false,true,3,false,10);
    policy.sample(true,false,true,4,false,11);
    assert(policy.sample(false,true,true,4,false,11.05));assert(policy.hidden);
    policy.sample(false,true,true,5,false,12);assert(policy.hidden); // repeated native show while loading
    policy.sample(false,false,true,5,false,13);assert(policy.hidden); // no end-of-load flash
    assert(policy.sample(false,false,false,5,false,13.1));assert(!policy.hidden); // gameplay acknowledges hide

    policy.sample(true,false,true,6,false,15);
    policy.sample(true,true,true,6,false,15.1);assert(policy.hidden);
    policy.sample(true,false,true,6,false,16);assert(!policy.hidden); // failed/cancelled travel restores map

    policy.sample(true,false,true,7,false,20);
    policy.sample(false,true,true,7,false,20.1);
    policy.sample(false,false,true,7,false,21);assert(policy.hidden);
    policy.sample(false,false,true,8,false,21.1);assert(!policy.hidden); // new menu requests cursor

    policy.sample(true,false,true,8,false,25);
    policy.sample(false,true,true,8,false,25.1);
    policy.sample(false,false,true,8,true,26);assert(!policy.hidden); // shortcut wheel restored

    policy.sample(true,false,true,9,false,30);
    policy.sample(false,true,true,9,false,30.1);
    policy.sample(false,false,true,9,false,31);assert(policy.hidden);
    policy.sample(false,false,true,9,false,33.1);assert(!policy.hidden); // bounded fallback
    puts("Teleport cursor: map selections preserved, loading hides, gameplay/menu restore and stale-state fallback passed.");
}
