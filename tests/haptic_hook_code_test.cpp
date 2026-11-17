#include "../genshin/haptic_hook_code.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>

extern "C" uint64_t GDMHapticRelocationTestValue;
uint64_t GDMHapticRelocationTestValue=37;
extern "C" __attribute__((naked,noinline)) uint64_t GDMHapticRelocationTestFunction(uint64_t) {
    asm("stp x20, x19, [sp, #-32]!\n"
        "stp x29, x30, [sp, #16]\n"
        "add x29, sp, #16\n"
        "adrp x19, _GDMHapticRelocationTestValue@PAGE\n"
        "add x19, x19, _GDMHapticRelocationTestValue@PAGEOFF\n"
        "ldr x8, [x19]\n"
        "add x0, x0, x8\n"
        "ldp x29, x30, [sp, #16]\n"
        "ldp x20, x19, [sp], #32\n"
        "ret\n");
}
int main(int argc,char **argv) {
    assert(argc==2);std::ifstream fixture(argv[1],std::ios::binary);assert(fixture);
    unsigned checked=0,pages=0;
    while(true) {
        uint64_t source;uint32_t words[4];
        if(!fixture.read((char *)&source,8))break;
        assert(fixture.read((char *)words,16));HapticHookCode generated;
        assert(BuildHapticHookCode(words,source,generated));assert(generated.length<=generated.bytes.size());
        pages+=generated.relocatedPages;++checked;
    }
    assert(checked==8 && pages==2); // four actual methods in each client
    uint32_t support[4]={0xa9be4ff4,0xa9017bfd,0x910043fd,0x90038db3};
    HapticHookCode generated;
    assert(BuildHapticHookCode(support,0x10ea06730,generated));
    uintptr_t literal;memcpy(&literal,generated.bytes.data()+20,8);assert(literal==0x115bba000);
    support[3]=0xf0fffff3; // ADRP x19, preceding page (negative signed displacement)
    assert(BuildHapticHookCode(support,0x100001ff4,generated));
    memcpy(&literal,generated.bytes.data()+20,8);assert(literal==0x100001000);
    for(uint32_t refused:{0x14000001u,0x94000001u,0xd65f03c0u,0x10000013u,0x58000050u,0x54000000u,0x34000000u}) {
        support[3]=refused;assert(!BuildHapticHookCode(support,0x100000000,generated));
    }
    uint32_t tail[]={0x1e2703e1,0x14000001};
    assert(HapticUntimedTail(tail,0x1000,0x1008));assert(!HapticUntimedTail(tail,0x1000,0x100c));
    tail[1]=0x17ffffff;assert(HapticUntimedTail(tail,0x1000,0x1000));
    tail[0]=0xd503201f;assert(!HapticUntimedTail(tail,0x1000,0x1000));

    // Execute the production relocation builder on real ARM64 instructions.
    // The copied function reads the original data page and preserves its stack.
    uintptr_t source=(uintptr_t)GDMHapticRelocationTestFunction;
    assert(BuildHapticHookCode((const uint32_t *)source,source,generated));assert(generated.relocatedPages==1);
    void *code=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);assert(code!=MAP_FAILED);
    memcpy(code,generated.bytes.data(),generated.length);sys_icache_invalidate(code,generated.length);
    assert(!mprotect(code,4096,PROT_READ|PROT_EXEC));
    for(uint64_t value=0;value<100;++value)assert(((uint64_t (*)(uint64_t))code)(value)==value+37);
    GDMHapticRelocationTestValue=91;assert(((uint64_t (*)(uint64_t))code)(4)==95);
    munmap(code,4096);
    std::cout<<"Eight actual CN/global prefixes, signed ADRP, preserved 8-byte tail, unsafe opcode rejection and executed ARM64 relocation passed; no haptic hardware output\n";
}
