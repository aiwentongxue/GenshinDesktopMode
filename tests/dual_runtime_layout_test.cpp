#include "runtime_image_fixture.h"
#include <iostream>

static void Check(const char *path,GenshinRuntimeLayout expected) {
    Image image(path);
    auto reader=[&](uintptr_t a,size_t n,bool w){return image.read(a,n,w);};
    uintptr_t mismatch=0;
    assert(SelectGenshinRuntimeLayout(reader,mismatch)==expected);
    // Each callable binding is covered by exact code bytes in its own profile.
    auto checkBinding=[&](GenshinAddressBinding b) {
        uintptr_t actual=ResolveGenshinAddress(expected,b.logical);
        assert(actual);
        if(b.logical==0x115644a70) {assert(image.read(actual,sizeof(void *),true));return;}
        const auto *p=GenshinExpectedPattern(expected,b.logical);assert(p && p->address==actual);
        assert(!memcmp(image.read(actual,96,false),p->code,96));
    };
    if(expected==GenshinRuntimeLayout::ModernInput) {
        for(auto b:GenshinModernAddressBindings) checkBinding(b);
    } else {
        for(auto b:GenshinGlobalAddressBindings) checkBinding(b);
    }
    // Corrupt each of the functional hook sites independently. A UUID-only
    // repack was accepted above, but even one changed instruction is refused.
    for(uintptr_t logical:{0x10218d388ULL,0x10342b2c0ULL,0x108a31778ULL,0x10eec3098ULL,
                          0x10f7f8884ULL,0x10f7f88d4ULL,0x10e9bb6a0ULL,0x10023942cULL,
                          0x10f8014dcULL,0x10f80155cULL,0x10eba1090ULL}) {
        auto *code=const_cast<uint8_t *>(image.read(ResolveGenshinAddress(expected,logical),96,false));
        assert(code);code[44]^=1;
        assert(SelectGenshinRuntimeLayout(reader,mismatch)==GenshinRuntimeLayout::Unsupported);
        code[44]^=1;
        assert(SelectGenshinRuntimeLayout(reader,mismatch)==expected);
    }
    image.dataPresent=false;
    assert(SelectGenshinRuntimeLayout(reader,mismatch)==GenshinRuntimeLayout::Unsupported);
    assert(!ResolveGenshinAddress(GenshinRuntimeLayout::Unsupported,0x10f057bd4));
    assert(!ResolveGenshinAddress(GenshinRuntimeLayout::GlobalInput,0x103a3894c));
    assert(!ResolveGenshinAddress(GenshinRuntimeLayout::GlobalInput,0x123456789));
    std::cout<<(expected==GenshinRuntimeLayout::ModernInput?"modern 7.2":expected==GenshinRuntimeLayout::GlobalInput?"global":"mainland")
        <<": exact bindings accepted; changed UUID accepted; changed hook/code/data and unmapped addresses refused\n";
}
int main(int argc,char **argv) {
    assert(argc==4);
    Check(argv[1],GenshinRuntimeLayout::MainlandInput);
    Check(argv[2],GenshinRuntimeLayout::GlobalInput);
    Check(argv[3],GenshinRuntimeLayout::ModernInput);
}
