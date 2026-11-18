#include "screen_metrics.h"
#include <atomic>
#include <cassert>
#include <thread>

int main() {
    DesktopScreenMetrics metrics;
    assert(metrics.read().width == 0);
    metrics.publish(3840,2160);
    metrics.publish(0,0);
    assert(metrics.read().height == 2160);
    std::atomic<bool> done{false};
    std::thread resize([&] {
        for (unsigned i=0; i<100000; ++i) {
            metrics.publish(3456,2234);
            metrics.publish(5120,2880);
        }
        done=true;
    });
    do {
        auto [w,h]=metrics.read();
        assert((w==3840 && h==2160) || (w==3456 && h==2234) || (w==5120 && h==2880));
    } while (!done.load());
    resize.join();
    assert(metrics.read().width == 5120);
}
