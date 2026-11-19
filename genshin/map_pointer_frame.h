#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

// AppKit publishes absolute drag events. The map and cursor share this viewport.
class DesktopMapPointerFrame {
public:
    struct Snapshot {
        double x=0,y=0,width=0,height=0;
        uint64_t focus=0,serial=0;
        bool active=false;
        bool position(double screenX,double screenY,double &u,double &v) const {
            if(!active||!std::isfinite(screenX)||!std::isfinite(screenY))return false;
            u=(screenX-x)/width;v=1.0-(screenY-y)/height;
            return std::isfinite(u)&&std::isfinite(v);
        }
    };
private:
    mutable std::mutex lock;
    Snapshot value;
    double frameX=0,frameY=0;
    uint64_t pointSerial=0;
    std::atomic<bool> available{false};
public:
    bool ready() const {return available.load(std::memory_order_acquire);}
    void cancel() {
        std::lock_guard<std::mutex> guard(lock);
        if(value.active){value.active=false;++value.serial;}
        available.store(false,std::memory_order_release);
    }
    bool publish(double x,double y,double width,double height,uint64_t focus) {
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0){cancel();return false;}
        std::lock_guard<std::mutex> guard(lock);
        if(!value.active||x!=value.x||y!=value.y||width!=value.width||height!=value.height||focus!=value.focus)++value.serial;
        value.x=x;value.y=y;value.width=width;value.height=height;value.focus=focus;value.active=true;
        available.store(true,std::memory_order_release);return true;
    }
    Snapshot snapshot() const {std::lock_guard<std::mutex> guard(lock);return value;}
    bool current(const Snapshot &s,uint64_t focus) const {
        std::lock_guard<std::mutex> guard(lock);
        return s.active&&value.active&&s.serial==value.serial&&s.focus==focus;
    }
    bool record(const Snapshot &s,uint64_t focus,double screenX,double screenY) {
        std::lock_guard<std::mutex> guard(lock);
        if(!s.active||!value.active||s.serial!=value.serial||s.focus!=focus)return false;
        pointSerial=s.serial;frameX=screenX;frameY=screenY;return true;
    }
    bool drawPoint(double &screenX,double &screenY) const {
        std::lock_guard<std::mutex> guard(lock);
        if(!value.active||pointSerial!=value.serial)return false;
        screenX=frameX;screenY=frameY;return true;
    }
};
