#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

// Derive the actual output-report length from the device's HID descriptor.
// Input/feature reports do not contribute; global Push/Pop is honored.
static inline size_t PSUSBOutputReportSize(const uint8_t *data,size_t length,uint8_t wanted) {
    struct Globals {uint32_t bits=0,count=0,id=0;} state,stack[16];
    unsigned depth=0;uint64_t total=0;
    if(!data||length>65536)return 0;
    for(size_t i=0;i<length;) {
        uint8_t prefix=data[i++];
        if(prefix==0xfe) {
            if(i+2>length)return 0;size_t size=data[i];i+=2;
            if(size>length-i)return 0;i+=size;continue;
        }
        unsigned size=prefix&3;if(size==3)size=4;
        if(size>length-i)return 0;
        uint32_t value=0;for(unsigned j=0;j<size;++j)value|=uint32_t(data[i++])<<(8*j);
        unsigned type=(prefix>>2)&3,tag=prefix>>4;
        if(type==1) {
            if(tag==7)state.bits=value;
            if(tag==8) {if(!value||value>255)return 0;state.id=value;}
            if(tag==9)state.count=value;
            if(tag==10) {if(depth==16)return 0;stack[depth++]=state;}
            if(tag==11) {if(!depth)return 0;state=stack[--depth];}
        } else if(type==0&&tag==9&&state.id==wanted) {
            total+=uint64_t(state.bits)*state.count;
            if(total>65536*8)return 0;
        }
    }
    return total?(total+7)/8+(wanted?1:0):0;
}

struct PSUSBAudioModeReport {
    std::array<uint8_t,64> bytes{};size_t length=0;
    bool build(size_t actualLength,bool initializePower=false,bool microphoneMuted=true) {
        *this={};if(actualLength!=48&&actualLength!=64)return false;
        length=actualLength;bytes[0]=0x02;
        // SDL's zero-motor effect leaves both rumble-emulation bits clear to
        // restore audio haptics. LEDs, triggers, routing and volumes stay
        // untouched. An initial power-save update additionally clears haptic
        // mute (bit 7), preserving a known mic mute or conservatively muting it.
        if(initializePower) {bytes[2]=0x02;bytes[10]=microphoneMuted?0x10:0x00;}
        return true;
    }
};
