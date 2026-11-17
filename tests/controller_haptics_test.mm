#import <Foundation/Foundation.h>
#include "../genshin/controller_haptic_pattern.h"
#include <cassert>
#include <limits>

// Hardware-free checks: no GCController query, engine creation or playback.
int main() {@autoreleasepool {
    ControllerHapticState state;
    assert(state.set(0,.4f,1,10));
    assert(state.set(1,.8f,2,10));
    state.expire(11);
    assert(state.output(0).level==0 && state.output(1).level==.8f);
    state.expire(12);assert(state.output(1).level==0);
    assert(state.set(0,1,0,20));
    state.expire(100000);assert(state.output(0).level==1 && state.nextDeadline()==0);
    assert(state.set(1,.3f,0,100000,true));assert(state.output(0).level==0);
    assert(!state.set(2,1,1,100000));
    assert(!state.set(0,1,-1,100000));
    assert(!state.set(0,1,std::numeric_limits<double>::infinity(),100000));
    assert(state.set(0,std::numeric_limits<float>::quiet_NaN(),0,100000));
    assert(state.output(0).level==0);
    state.stop();assert(state.nextDeadline()==0 && state.output(1).level==0);
    assert(state.pulse(250,.7f,.6f,30));
    assert(std::abs(state.nextDeadline()-30.25)<1e-6);
    assert(state.set(0,.4f,0,30));
    state.expire(30.25);assert(state.output(0).level==.4f && state.output(1).level==0);
    assert(state.pulse(100000,3,-1,40));
    assert(state.nextDeadline()==45 && state.output(1).level==1 && state.output(1).sharpness==0);
    state.stop();
    unsigned patterns=0;
    for(float level:{.01f,.25f,.5f,1.f})for(float sharpness:{0.f,.2f,.8f,1.f}) {
        NSError *error=nil;
        CHHapticPattern *pattern=ControllerMotorPattern({level,sharpness,0},&error);
        assert(pattern && !error);
        NSDictionary *exported=[pattern exportDictionaryAndReturnError:&error];
        assert(exported && !error);
        assert([exported[CHHapticPatternKeyPattern] count]==3);
        ++patterns;
    }
    printf("Haptic state checks passed; %u production patterns constructed/exported; no hardware output\n",patterns);
    return 0;
}}
