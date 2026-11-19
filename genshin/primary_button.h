#pragma once
#include <array>
#include <cstddef>

// AppKit can deliver both halves of a trackpad tap between two Unity updates.
// Present each transition for one input frame, preserving its event position.
class DesktopPrimaryButton {
public:
    struct Point { float x=0, y=0; bool valid=false; };
    struct Frame { bool down=false; Point position{}; };

    void cancel() {
        first=count=0;
        received=delivered=false;
        sawPhysicalHold=false;missingUpFrames=0;
        neutral=true;
    }

    void event(bool down, bool eligible, Point point) {
        if (down==received || (down && !eligible)) return;
        if (count==queue.size()) {
            // A stalled client must not replay a burst of old attacks.
            cancel();
            return;
        }
        received=down;
        sawPhysicalHold=false;missingUpFrames=0;
        queue[(first+count)%queue.size()]={down,point};
        ++count;
    }

    Frame frame(bool physicalDown=false) {
        // Repair a lost release only for an observed physical hold. The
        // physical snapshot can end that press, but can never start one.
        if(received && physicalDown) {sawPhysicalHold=true;missingUpFrames=0;}
        else if(received && sawPhysicalHold && ++missingUpFrames>=2) event(false,true,{});
        if (neutral) { neutral=false; return {}; }
        if (!count) return {delivered,{}};
        Frame edge=queue[first];
        first=(first+1)%queue.size();
        --count;
        delivered=edge.down;
        return edge;
    }

private:
    std::array<Frame,8> queue{};
    std::size_t first=0,count=0;
    bool received=false,delivered=false,neutral=false;
    bool sawPhysicalHold=false;
    unsigned missingUpFrames=0;
};
