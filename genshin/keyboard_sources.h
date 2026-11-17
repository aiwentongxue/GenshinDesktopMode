#pragma once
#include <array>
#include <atomic>

// AppKit delivers real release events even when Catalyst's GC snapshot keeps
// a key pressed. A seen event remains authoritative until focus is lost.
struct DesktopKeyboardSources {
    std::array<std::atomic<unsigned char>,350> native{}; // 0 unknown, 1 up, 2 down
    void event(int key,bool down) {
        if(key>0 && key<323) native[key].store(down?2:1,std::memory_order_relaxed);
    }
    bool resolve(int key,bool gc) const {
        auto value=native[key].load(std::memory_order_relaxed);
        return value?value==2:gc;
    }
    void reset() {for(auto &value:native) value.store(0,std::memory_order_relaxed);}
};
