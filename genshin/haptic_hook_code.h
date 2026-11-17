#pragma once
#include <array>
#include <cstdint>
#include <cstring>

// Rumble hooks alone need an ADRP in the first four instructions on 7.2.
// Other relative instructions and short tail-call veneers remain unpatchable.
struct HapticHookCode {
    std::array<uint8_t,96> bytes{};
    size_t length=0;
    unsigned relocatedPages=0;
    void word(uint32_t value) {memcpy(bytes.data()+length,&value,4);length+=4;}
    void pointer(uintptr_t value) {memcpy(bytes.data()+length,&value,8);length+=8;}
};
static inline bool BuildHapticHookCode(const uint32_t *prefix,uintptr_t source,HapticHookCode &out) {
    out={};
    for(unsigned index=0;index<4;++index) {
        uint32_t op=prefix[index];
        bool storePair=(op&0x3b400000u)==0x29000000u && ((op>>5)&31)==31;
        bool stackSubtract=(op&0xffc003ffu)==0xd10003ffu;
        bool framePointer=(op&0xffc003ffu)==0x910003fdu;
        bool moveRegister=(op&0xffe0ffe0u)==0xaa0003e0u && (op&31)!=16;
        if(storePair||stackSubtract||framePointer||moveRegister) {out.word(op);continue;}
        if((op&0x9f000000u)!=0x90000000u || (op&31)==16 || (op&31)==31) {out={};return false;}
        int64_t immediate=((op>>5)&0x7ffffu)*4+((op>>29)&3u);
        if(immediate&(1<<20))immediate-=1<<21;
        uintptr_t page=((source+index*4)&~uintptr_t(4095))+(uintptr_t)(immediate*4096);
        // LDR the original page into the same register, then skip its literal.
        // No scratch register or condition flag is changed by relocation.
        out.word(0x58000040u|(op&31));out.word(0x14000003u);out.pointer(page);
        ++out.relocatedPages;
    }
    out.word(0x58000050u);out.word(0xd61f0200u);out.pointer(source+16);
    return true;
}
static inline bool HapticUntimedTail(const uint32_t *prefix,uintptr_t source,uintptr_t timed) {
    if(prefix[0]!=0x1e2703e1u || (prefix[1]&0xfc000000u)!=0x14000000u)return false;
    int64_t displacement=prefix[1]&0x03ffffffu;
    if(displacement&(1<<25))displacement-=1<<26;
    return source+4+(uintptr_t)(displacement*4)==timed;
}
