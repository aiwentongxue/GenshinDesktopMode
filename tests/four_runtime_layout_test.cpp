#include "runtime_image_fixture.h"
#include <iostream>
static void Check(const char *path,GenshinRuntimeLayout expected) {
    Image image(path);
    auto reader=[&](uintptr_t address,size_t length,bool writable){return image.read(address,length,writable);};
    uintptr_t mismatch=0;
    assert(SelectGenshinRuntimeLayout(reader,mismatch)==expected);
    auto binding=[&](GenshinAddressBinding b) {
        auto actual=ResolveGenshinAddress(expected,b.logical);assert(actual==b.actual);
        if(b.logical==0x115644a70) {assert(image.read(actual,8,true));return;}
        auto pattern=GenshinExpectedPattern(expected,b.logical);assert(pattern&&pattern->address==actual);
        assert(!memcmp(image.read(actual,96,false),pattern->code,96));
    };
    if(expected==GenshinRuntimeLayout::ModernInput)for(auto b:GenshinModernAddressBindings)binding(b);
    if(expected==GenshinRuntimeLayout::GlobalModernInput)for(auto b:GenshinGlobalModernAddressBindings)binding(b);
    if(expected==GenshinRuntimeLayout::GlobalInput)for(auto b:GenshinGlobalAddressBindings)binding(b);
    for(uintptr_t logical:{0x10218d388ULL,0x10342b2c0ULL,0x108a31778ULL,0x10eec3098ULL,
        0x10f7f8884ULL,0x10f7f88d4ULL,0x10e9bb6a0ULL,0x10023942cULL,0x10f8014dcULL,0x10f80155cULL}) {
        auto code=const_cast<uint8_t *>(image.read(ResolveGenshinAddress(expected,logical),96,false));assert(code);
        code[44]^=1;assert(SelectGenshinRuntimeLayout(reader,mismatch)==GenshinRuntimeLayout::Unsupported);
        code[44]^=1;assert(SelectGenshinRuntimeLayout(reader,mismatch)==expected);
    }
    image.dataPresent=false;assert(SelectGenshinRuntimeLayout(reader,mismatch)==GenshinRuntimeLayout::Unsupported);
    if(expected!=GenshinRuntimeLayout::MainlandInput) assert(!ResolveGenshinAddress(expected,0x123456789));
    std::cout<<"profile "<<(int)expected<<": exact bindings and UUID independence passed; corrupt code/data rejected\n";
}
int main(int argc,char **argv) {
    assert(argc==5);
    static_assert(GenshinMethodCodeWord(GenshinRuntimeLayout::ModernInput)==0);
    static_assert(GenshinMethodCodeWord(GenshinRuntimeLayout::GlobalModernInput)==1);
    static_assert(GenshinMethodFlagsOffset(GenshinRuntimeLayout::ModernInput)==0x2c);
    static_assert(GenshinMethodFlagsOffset(GenshinRuntimeLayout::GlobalModernInput)==0x28);
    static_assert(GenshinModernKeyboardPolicy(GenshinRuntimeLayout::ModernInput));
    static_assert(GenshinModernKeyboardPolicy(GenshinRuntimeLayout::GlobalModernInput));
    static_assert(!GenshinModernKeyboardPolicy(GenshinRuntimeLayout::GlobalInput));
    Check(argv[1],GenshinRuntimeLayout::MainlandInput);
    Check(argv[2],GenshinRuntimeLayout::GlobalInput);
    Check(argv[3],GenshinRuntimeLayout::ModernInput);
    Check(argv[4],GenshinRuntimeLayout::GlobalModernInput);
}
