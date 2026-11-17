#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

struct GenshinRuntimePattern {
    uintptr_t address;
    uint8_t code[96];
};

// Reader returns readable code or a writable data range, with bounds checked
// against the actual Mach-O image. No UUID, region, or version is consulted.
template<class Reader>
static bool MatchesGenshinRuntimeLayout(const GenshinRuntimePattern *patterns,size_t count,
                                       Reader reader,uintptr_t &mismatch,
                                       uintptr_t staticsAddress=0x115644a70) {
    mismatch=0;
    if(!patterns || !count) return false;
    for(size_t i=0;i<count;++i) {
        const auto &pattern=patterns[i];
        const uint8_t *code=reader(pattern.address,sizeof(pattern.code),false);
        if(!code || memcmp(code,pattern.code,sizeof(pattern.code))) {
            mismatch=pattern.address;return false;
        }
    }
    if(!reader(staticsAddress,sizeof(void *),true)) {mismatch=staticsAddress;return false;}
    return true;
}
