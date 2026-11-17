#pragma once

// Distance gains are restricted to the identified UI. Hardware accumulation,
// frame timing, pointer speed and map dragging keep their paths.
struct DesktopWheelZoomPolicy {
    // 0 ordinary UI, 1 map, 2 world camera, 3 Paimon menu, 4 settings list, 5 friend list.
    static float gain(bool mos,unsigned zoom) {
        return zoom==5?(mos?4.0f:1.5f):zoom==4?(mos?4.0f:1.5f):zoom==3?(mos?4.0f:1.0f):zoom==2?(mos?4.8f:1.8f):zoom==1?(mos?4.0f:1.5f):1.0f;
    }
};
