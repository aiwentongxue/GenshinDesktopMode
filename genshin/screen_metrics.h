#pragma once
#include <atomic>
#include <cstdint>

struct DesktopScreenSize {
    int width, height;
};

class DesktopScreenMetrics {
    std::atomic<uint64_t> pixels{0};
public:
    void publish(int width, int height) {
        if (width > 0 && height > 0)
            pixels.store((uint64_t(uint32_t(width)) << 32) | uint32_t(height), std::memory_order_relaxed);
    }
    DesktopScreenSize read() const {
        const uint64_t value = pixels.load(std::memory_order_relaxed);
        return {int(value >> 32), int(uint32_t(value))};
    }
};
