#pragma once
#include <cstddef>
#include "native_trigger_command.h"
namespace PSTriggerReport {
struct Packet {std::array<uint8_t,78> bytes{};size_t length=0;};
inline uint32_t CRC(const uint8_t *p,size_t n) {
    uint32_t v=0xffffffff;
    auto byte=[&](uint8_t b){v^=b;for(unsigned i=0;i<8;++i)v=(v>>1)^(0xedb88320u&uint32_t(-int(v&1)));};
    byte(0xa2);for(size_t i=0;i<n;++i)byte(p[i]);return ~v;
}
// Mask bit 0 is left, bit 1 is right, as in the original ScePad call.
// Only trigger-valid bits are set; LED, volume, mute and routing flags stay 0.
inline Packet Make(bool bluetooth,size_t usbLength,unsigned mask,const NativeTrigger::Pair &effects) {
    Packet p;if(!mask||(mask&~3u))return p;
    if(!bluetooth&&usbLength!=48&&usbLength!=64)return p;
    p.length=bluetooth?78:usbLength;p.bytes[0]=bluetooth?0x31:2;
    size_t offset=bluetooth?3:1;
    if(bluetooth)p.bytes[2]=0x10;
    for(unsigned side=0;side<2;++side)if(mask&(1u<<side)) {
        auto effect=NativeTrigger::Encode(effects[side]);
        p.bytes[offset]|=side?4:8;
        size_t start=offset+(side?10:21);
        for(unsigned i=0;i<11;++i)p.bytes[start+i]=effect[i];
    }
    if(bluetooth) {uint32_t crc=CRC(p.bytes.data(),74);for(unsigned i=0;i<4;++i)p.bytes[74+i]=uint8_t(crc>>(i*8));}
    return p;
}
}
