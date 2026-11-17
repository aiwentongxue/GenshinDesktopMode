#include "runtime_image_fixture.h"
#include "../genshin/runtime_discovery_image.h"
#include <array>
#include <iostream>
using namespace GenshinRuntimeDiscovery;

static uint32_t Branch(uintptr_t from,uintptr_t to,bool link) {
    int64_t delta=(int64_t)to-(int64_t)from;
    assert(!(delta&3) && delta>=-0x8000000 && delta<0x8000000);
    return (link?0x94000000u:0x14000000u)|((uint32_t)(delta/4)&0x03ffffffu);
}
struct Fixture {
    Image image;
    struct Block {uintptr_t address;std::vector<uint8_t> bytes;bool scanned;};
    std::vector<Block> blocks;
    explicit Fixture(const char *path):image(path){}
    const uint8_t *read(uintptr_t a,size_t n,bool writable) {
        if(!writable)for(auto &b:blocks)
            if(a>=b.address && a-b.address<=b.bytes.size() && n<=b.bytes.size()-(a-b.address))
                return b.bytes.data()+a-b.address;
        return image.read(a,n,writable);
    }
    void write(uintptr_t a,uint32_t v) {auto *p=const_cast<uint8_t *>(read(a,4,false));assert(p);memcpy(p,&v,4);}
    Bindings select() {
        auto reader=[&](uintptr_t a,size_t n,bool w){return read(a,n,w);};
        std::vector<CodeSpan> spans;
        assert(CodeSpans(image.bytes.data(),image.bytes.size(),reader,spans));
        for(auto &b:blocks)if(b.scanned)spans.push_back({b.address,b.bytes.data(),b.bytes.size()});
        uintptr_t mismatch;return Select(reader,spans.data(),spans.size(),mismatch);
    }
    uintptr_t keyBody(GenshinRuntimeLayout layout) {
        uintptr_t body=ResolveGenshinAddress(layout,GetKeyLogical);
        for(unsigned i=0;i<5;++i) {
            uint32_t w=Word(read(body,4,false));
            if((w&0xfc000000u)!=0x14000000u)return body;
            body=BranchTarget(body,w,false);
        }
        assert(false);return 0;
    }
    void rejected(const char *name) {
        auto b=select();assert(b.layout==GenshinRuntimeLayout::Unsupported);
        assert(!Resolve(b,LayoutLogical) && !Resolve(b,InputGetterLogical));
        std::cout<<"rejected: "<<name<<'\n';
    }
};
static void Check(const char *path,GenshinRuntimeLayout layout) {
    Fixture f(path);auto baseline=f.select();
    assert(baseline.layout==layout && baseline.dynamic && baseline.layoutMatches==1);
    uintptr_t originalLayout=ResolveGenshinAddress(layout,LayoutLogical);
    uintptr_t originalGetter=ResolveGenshinAddress(layout,InputGetterLogical);
    assert(Resolve(baseline,LayoutLogical)==originalLayout && Resolve(baseline,InputGetterLogical)==originalGetter);
    assert(Resolve(baseline,0x10eec3098)==ResolveGenshinAddress(layout,0x10eec3098));
    std::cout<<"profile "<<(int)layout<<": actual image discovery and unchanged input-update binding passed\n";
    auto change=[&](uintptr_t a,uint32_t value,const char *name){
        uint32_t before=Word(f.read(a,4,false));f.write(a,value);f.rejected(name);f.write(a,before);
    };
    change(originalLayout+36,0xd503201f,"missing layout signature");
    change(originalLayout+32,0x94000000,"layout BL targets itself");
    change(originalLayout+24,0x14000000,"wrong call opcode");
    change(originalLayout,0xd503201f,"wrong selector prologue");
    change(ResolveGenshinAddress(layout,0x10eec3098),0xd503201f,"unrelated ABI fingerprint changed");
    uintptr_t body=f.keyBody(layout);
    change(body+28,0x94000000,"input call targets itself");
    change(body+32,0xf9404008,"wrong input structure offsets");
    change(originalGetter,0x52800000,"getter changes manager selection");
    f.image.dataPresent=false;f.rejected("missing writable statics");f.image.dataPresent=true;
    const auto *selector=f.read(originalLayout,96,false);
    f.blocks.push_back({originalLayout+0x1000,{selector,selector+96},true});
    f.rejected("duplicate signature, including unvalidated duplicate");f.blocks.clear();

    // Simulate an update relocating both supported entries. Adjust only real
    // branch instructions, leave other ABI fingerprints/behavior unchanged,
    // and remove old entries so a fixed-address implementation cannot pass.
    uintptr_t movedLayout=originalLayout+0x40000,movedGetter=originalGetter+0x40000;
    f.blocks.push_back({movedLayout,{selector,selector+96},true});
    for(size_t offset:{24u,32u}) {
        uintptr_t target=BranchTarget(originalLayout+offset,Word(selector+offset),true);
        f.write(movedLayout+offset,Branch(movedLayout+offset,target,true));
    }
    const uint8_t *getter=f.read(originalGetter,8,false);
    uintptr_t worker=BranchTarget(originalGetter+4,Word(getter+4),false);
    f.blocks.push_back({movedGetter,{getter,getter+8},false});
    f.write(movedGetter+4,Branch(movedGetter+4,worker,false));
    f.write(body+28,Branch(body+28,movedGetter,true));
    f.write(originalLayout+36,0);f.write(originalGetter,0);
    f.rejected("other native readers retain old getter calls");
    const auto *patterns=layout==GenshinRuntimeLayout::ModernInput?GenshinModernRuntimePatterns:GenshinGlobalModernRuntimePatterns;
    size_t count=layout==GenshinRuntimeLayout::ModernInput?sizeof(GenshinModernRuntimePatterns)/sizeof(*patterns):
        sizeof(GenshinGlobalModernRuntimePatterns)/sizeof(*patterns);
    unsigned callers=0;
    for(size_t i=0;i<count;++i)for(size_t j=0;j<sizeof(patterns[i].code);j+=4) {
        uint32_t before=Word(patterns[i].code+j);bool link=(before&0xfc000000u)==0x94000000u;
        uintptr_t target=BranchTarget(patterns[i].address+j,before,link);
        if(target==originalGetter) {
            f.write(patterns[i].address+j,Branch(patterns[i].address+j,movedGetter,link));++callers;
        }
    }
    assert(callers==3);
    auto moved=f.select();assert(moved.dynamic && moved.layout==layout);
    assert(Resolve(moved,LayoutLogical)==movedLayout && Resolve(moved,InputGetterLogical)==movedGetter);
    uintptr_t mismatch=0;
    auto reader=[&](uintptr_t a,size_t n,bool w){return f.read(a,n,w);};
    assert(SelectGenshinRuntimeLayout(reader,mismatch)==GenshinRuntimeLayout::Unsupported);
    std::cout<<"relocated entries accepted and actually returned; old exact-address selector rejected\n";
    change(movedGetter+4,Branch(movedGetter+4,worker+4,false),"relocated getter reaches wrong worker");
    change(movedLayout+32,Word(selector+32),"relocated selector retains stale BL immediate");

    std::vector<CodeSpan> spans;
    auto *header=(mach_header_64 *)f.image.bytes.data();
    uint32_t commands=header->sizeofcmds;header->sizeofcmds=UINT32_MAX;
    assert(!CodeSpans(header,f.image.bytes.size(),reader,spans));header->sizeofcmds=commands;
    assert(!CodeSpans(header,sizeof(*header)-1,reader,spans));
    auto *command=(load_command *)(header+1);uint32_t size=command->cmdsize;command->cmdsize=0;
    assert(!CodeSpans(header,f.image.bytes.size(),reader,spans));command->cmdsize=size;
    assert(!BranchTarget(0x1000,0x94000000,false));
    assert(!BranchTarget(0x1001,0x14000000,false));
    assert(!BranchTarget(4,0x17fffffe,false));
    std::cout<<"malformed Mach-O, branch bounds and non-branch rejection passed\n";
}
int main(int argc,char **argv) {
    assert(argc==3);
    Check(argv[1],GenshinRuntimeLayout::ModernInput);
    Check(argv[2],GenshinRuntimeLayout::GlobalModernInput);
}
