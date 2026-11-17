#include "../genshin/control_device_policy.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace DesktopControlDevicePolicy;
    // A visible keyboard selection must never reach the native Touch=0 branch.
    for(int mode:{0,1,2,3}) {
        assert(SelectedIndex(1,mode)==0);
        assert(InputMode(1,0)==2);
        assert(InputMode(1,1)==-1);
        assert(SelectedIndex(2,mode)==(mode==3?1:0));
        assert(InputMode(2,0)==2 && InputMode(2,1)==3);
    }
    for(int count:{-1,0,3,4}) {
        assert(!KnownOptions(count));
        assert(SelectedIndex(count,2)==-1);
        assert(InputMode(count,0)==-1);
    }
    for(int index:{-1,2,3}) assert(InputMode(2,index)==-1);
    // Device-only switches need a full desktop rebuild even for PC -> PC;
    // nested switches inside a real layout rebuild must not queue another.
    assert(NeedsDesktopRebuild(2,3,false));
    assert(NeedsDesktopRebuild(3,2,false));
    assert(!NeedsDesktopRebuild(2,3,true));
    assert(!NeedsDesktopRebuild(3,3,false));
    assert(!NeedsDesktopRebuild(0,0,false));
    assert(DesktopFallback(3,0)==2);
    assert(DesktopFallback(0,0)==0);
    assert(DesktopFallback(2,3)==3);
    std::cout<<"Device availability, no-touch routing, disconnect fallback and full-layout recursion guard passed\n";
}
