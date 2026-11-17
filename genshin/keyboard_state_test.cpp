#include "keyboard_state.h"
#include <cassert>
#include <cstdio>

int main() {
    KeyboardState s;KeyboardState::Keys physical{};
    s.focus(true);s.reconcile(physical);s.pending.clear();
    // Lost release is corrected by the next snapshot, with no timeout.
    s.event('w',true);physical['w']=true;s.reconcile(physical);
    for (int i=0;i<36000;++i) {s.reconcile(physical);assert(s.held['w']);}
    physical['w']=false;assert(s.reconcile(physical)==1);assert(!s.held['w']);
    // Both edges of a tap between frames survive, without leaving the key held.
    s.pending.clear();s.event('a',true);s.event('a',false);s.reconcile(physical);
    assert(s.pending.size()==2 && s.pending[0].second && !s.pending[1].second);
    // A release queued just before focus loss must still clear Unity's state.
    s.event('s',true);s.pending.clear();s.event('s',false);s.focus(false);
    bool releasesS=false;for(auto e:s.pending) if(e.first=='s'&&!e.second) releasesS=true;
    assert(releasesS);s.event('d',true);assert(!s.held['d']);
    // Held keys do not resume movement when returning from another application.
    physical['w']=true;s.focus(true);s.reconcile(physical);assert(!s.held['w']);
    s.event('w',true);s.reconcile(physical);assert(!s.held['w']);
    physical['w']=false;s.reconcile(physical);physical['w']=true;s.reconcile(physical);
    assert(s.held['w']);
    // Device disconnect releases all state, even if no key-up is delivered.
    s.release();physical.fill(false);s.reconcile(physical);assert(!s.held['w']);
    // Closing a native notice must not leak its Escape through the independent
    // physical-key snapshot or a queued GC press into the game behind it.
    s.pending.clear();s.event(27,true);s.blockUntilRelease(27);physical[27]=true;
    s.reconcile(physical);s.event(27,true);s.reconcile(physical);
    assert(!s.held[27]);for(auto e:s.pending) assert(e.first!=27 || !e.second);
    physical[27]=false;s.reconcile(physical);s.pending.clear();
    s.event(27,true);physical[27]=true;s.reconcile(physical);assert(s.held[27]);
    puts("PASS: missing key-up, long hold, rapid tap, focus loss, stale event, reconnect");
}
