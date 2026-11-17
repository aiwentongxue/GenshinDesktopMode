#include "mouse_button_state.h"
#include <cassert>
#include <iostream>
int main() {
    DesktopMouseButtons b;b.sample(0,true,true);
    for(int i=0;i<10000;++i) {auto f=b.sample(0,true,true);assert(!f.held&&!f.down&&!f.up);}
    auto f=b.sample(1,true,true);assert(f.held==1&&f.down==1&&!f.up);
    for(int i=0;i<1200;++i) {f=b.sample(1,i%2,true);assert(f.held==1&&!f.down&&!f.up);}
    f=b.sample(0,false,true);assert(!f.held&&!f.down&&f.up==1);
    f=b.sample(1,false,true);assert(!f.held&&!f.down);
    f=b.sample(1,true,true);assert(!f.held&&!f.down);
    b.sample(0,true,true);b.sample(1,true,true);b.cancel();
    f=b.sample(1,true,true);assert(!f.held&&!f.down&&f.up==1);
    f=b.sample(1,true,true);assert(!f.held&&!f.down&&!f.up);
    b.sample(0,true,true);b.sample(1,true,true);
    f=b.sample(1,true,false);assert(!f.held&&f.up==1);
    f=b.sample(1,true,true);assert(!f.held&&!f.down);
    b.sample(0,true,true);
    unsigned presses=0,releases=0;
    for(int i=0;i<30;++i) {
        for(int j=0;j<12;++j) {f=b.sample(1,true,true);presses+=(f.down&1)!=0;releases+=(f.up&1)!=0;}
        for(int j=0;j<12;++j) {f=b.sample(0,true,true);presses+=(f.down&1)!=0;releases+=(f.up&1)!=0;}
    }
    assert(presses==30&&releases==30);
    std::cout<<"Single mouse snapshot: idle, 1200 held frames without repeats, 30 press/release pairs and focus passed\n";
}
