#include "../genshin/keyboard_sources.h"
#include "../genshin/keyboard_state.h"
#include <cassert>
#include <iostream>
int main() {
    DesktopKeyboardSources sources;
    KeyboardState keys;keys.focus(true);KeyboardState::Keys physical{};keys.reconcile(physical);keys.pending.clear();
    // Real release arrives while Catalyst continues reporting A held.
    sources.event(97,true);keys.event(97,true);physical[97]=sources.resolve(97,true);keys.reconcile(physical);
    assert(keys.held[97]);
    sources.event(97,false);keys.event(97,false);
    for(int frame=0;frame<1200;++frame) {
        physical[97]=sources.resolve(97,true);keys.reconcile(physical);assert(!keys.held[97]);
    }
    // A complete tap between frames remains queued without leaving held state.
    keys.pending.clear();sources.event(97,true);keys.event(97,true);sources.event(97,false);keys.event(97,false);
    physical[97]=sources.resolve(97,true);keys.reconcile(physical);
    assert(keys.pending.size()==2 && keys.pending[0].second && !keys.pending[1].second && !keys.held[97]);
    // Focus change resets event authority; keys held on return remain blocked.
    sources.reset();keys.focus(false);keys.focus(true);physical[97]=true;keys.reconcile(physical);assert(!keys.held[97]);
    physical[97]=false;keys.reconcile(physical);sources.event(97,true);physical[97]=sources.resolve(97,false);keys.reconcile(physical);assert(keys.held[97]);
    for(int key:{97,100,115,119,9,27,32}) {
        sources.event(key,false);assert(!sources.resolve(key,true));sources.event(key,true);assert(sources.resolve(key,false));
    }
    std::cout<<"Native release overrides 1200 stale GC frames; short tap, focus handover and game keys passed\n";
}
