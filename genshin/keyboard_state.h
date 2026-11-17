#pragma once
#include <array>
#include <vector>
#include <utility>
#include <algorithm>

// Events retain short taps; the frame snapshot repairs a dropped key-up.
// A focus change discards queued input and requires held keys to be released.
struct KeyboardState {
    using Keys = std::array<bool,350>;
    Keys held{}, blocked{};
    std::vector<std::pair<int,bool>> pending;
    bool active=false;
    bool blockOnNextSnapshot=true;

    void blockUntilRelease(int key) {
        if(key<=0 || key>=323) return;
        bool releaseNeeded=held[key];
        pending.erase(std::remove_if(pending.begin(),pending.end(),[&](const auto &e) {
            if(e.first!=key) return false;
            releaseNeeded=true;return true;
        }),pending.end());
        held[key]=false;blocked[key]=true;
        if(releaseNeeded) pending.emplace_back(key,false);
    }

    void event(int key,bool down) {
        if (key<=0 || key>=323 || !active) return;
        if (!down) blocked[key]=false;
        if (down && (blocked[key] || blockOnNextSnapshot)) return;
        if (held[key]!=down) {held[key]=down;pending.emplace_back(key,down);}
    }
    void release() {
        pending.clear();
        // Include already queued releases: they may not have reached Unity yet.
        for (int key=1;key<323;++key) pending.emplace_back(key,false);
        held.fill(false);
        blockOnNextSnapshot=true;
    }
    void handoffToKeyboard() {
        // Release the old device's state while preserving real key packets
        // that requested this transition (including a short Escape tap).
        // Focus-loss blocks stay intact; an input-mode change is not a new
        // focus loss and must not block the newly pressed key until key-up.
        auto queued=pending;
        bool awaitingSnapshot=blockOnNextSnapshot;
        release();blockOnNextSnapshot=awaitingSnapshot;
        if(!awaitingSnapshot)
            for(auto event:queued)if(!blocked[event.first])this->event(event.first,event.second);
    }
    void focus(bool value) {
        if (active==value) return;
        active=value;
        release();
    }
    unsigned reconcile(const Keys &physical) {
        if (!active) return 0;
        if (blockOnNextSnapshot) {
            blocked=physical;
            blockOnNextSnapshot=false;
        }
        unsigned repaired=0;
        for (int key=1;key<323;++key) {
            if (!physical[key]) blocked[key]=false;
            bool down=physical[key] && !blocked[key];
            if (held[key]!=down) {++repaired;event(key,down);}
        }
        return repaired;
    }
};
