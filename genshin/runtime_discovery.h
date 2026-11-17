#pragma once
#include "runtime_layouts.h"
#include <limits>

// Discover only two entry points inside otherwise verified runtime ABIs.
// This does not make an unknown game's field offsets or metadata API safe.
namespace GenshinRuntimeDiscovery {
static constexpr uintptr_t LayoutLogical=0x10342b2c0;
static constexpr uintptr_t InputGetterLogical=0x10eec0180;
static constexpr uintptr_t GetKeyLogical=0x10e864028;
struct CodeSpan {uintptr_t address;const uint8_t *bytes;size_t size;};
struct LayoutSearch {uintptr_t entry=0;unsigned matches=0;};
struct Bindings {
    GenshinRuntimeLayout layout=GenshinRuntimeLayout::Unsupported;
    uintptr_t layoutEntry=0,inputGetter=0;
    unsigned layoutMatches=0;
    bool dynamic=false;
};
static uint32_t Word(const uint8_t *p) {uint32_t v;memcpy(&v,p,4);return v;}
static uintptr_t BranchTarget(uintptr_t pc,uint32_t word,bool link) {
    if((pc&3) || (word&0xfc000000u)!=(link?0x94000000u:0x14000000u))return 0;
    int64_t delta=(int64_t)(word&0x03ffffffu);
    if(delta&0x02000000)delta-=0x04000000;
    delta*=4;
    if(delta<0)return pc>=(uintptr_t)(-delta)?pc-(uintptr_t)(-delta):0;
    return pc<=UINTPTR_MAX-(uintptr_t)delta?pc+(uintptr_t)delta:0;
}
static LayoutSearch FindLayout(const CodeSpan *spans,size_t count) {
    static constexpr uint8_t signature[]={
        0xe8,0x03,0x00,0xaa,0x1f,0x2c,0x00,0x71,0x6d,0x01,0x00,0x54,
        0x1f,0x65,0x00,0x71,0xa0,0x00,0x00,0x54,0x1f,0x8d,0x00,0x71};
    LayoutSearch result;
    if(!spans || count>64)return result;
    for(size_t i=0;i<count;++i) {
        const auto &s=spans[i];
        if(!s.bytes || s.size<sizeof(signature) || s.size>UINTPTR_MAX-s.address)continue;
        size_t at=0,last=s.size-sizeof(signature);
        while(at<=last) {
            const auto *hit=(const uint8_t *)memchr(s.bytes+at,signature[0],last-at+1);
            if(!hit)break;
            size_t offset=(size_t)(hit-s.bytes);at=offset+1;
            if(((s.address+offset)&3) || memcmp(hit,signature,sizeof(signature)))continue;
            // Both supported modern ABIs have the same entry-to-switch prefix.
            // A signature too close to a section boundary is still a hit:
            // it must not be ignored to turn ambiguous code into a success.
            result.entry=offset>=36?s.address+offset-36:0;
            if(++result.matches>1){result.entry=0;return result;}
        }
    }
    return result;
}
template<class Reader>
static bool LayoutMatches(const GenshinRuntimePattern &expected,uintptr_t entry,Reader read) {
    const uint8_t *code=entry?read(entry,sizeof(expected.code),false):nullptr;
    if(!code)return false;
    for(size_t i=0;i<sizeof(expected.code);i+=4) {
        uint32_t before=Word(expected.code+i),after=Word(code+i);
        if(i==24 || i==32) {
            // Moving the selector changes its BL immediates, not its callees.
            // Verify both calls, including GetApplicationPlatform, exactly.
            uintptr_t target=BranchTarget(expected.address+i,before,true);
            if(!target || target!=BranchTarget(entry+i,after,true) || !read(target,4,false))return false;
        } else if(before!=after)return false;
    }
    return true;
}
template<class Reader>
static uintptr_t FindInputGetter(GenshinRuntimeLayout layout,Reader read) {
    uintptr_t body=ResolveGenshinAddress(layout,GetKeyLogical),seen[5]={};
    for(unsigned depth=0;depth<5;++depth) {
        const uint8_t *p=body?read(body,4,false):nullptr;if(!p)return 0;
        for(unsigned i=0;i<depth;++i)if(seen[i]==body)return 0;
        seen[depth]=body;
        uint32_t word=Word(p);
        if((word&0xfc000000u)!=0x14000000u)break;
        if(depth==4)return 0;
        body=BranchTarget(body,word,false);
    }
    // Validate the key bounds check and native key-bit reads before trusting
    // the first call. Never use an arbitrary first BL from an unknown body.
    static constexpr uint32_t shape[]={
        0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0x51000408,0x710a711f,0x54000168,0,
        0xf9404408,0x53057e69,0xb8695908,0xf940340a,0xb8695949,0x2a080128,0x1ad32508,0x12000100};
    const uint8_t *code=body?read(body,sizeof(shape),false):nullptr;if(!code)return 0;
    for(unsigned i=0;i<16;++i)if(i!=7 && Word(code+4*i)!=shape[i])return 0;
    uintptr_t getter=BranchTarget(body+28,Word(code+28),true);
    const auto *expected=GenshinExpectedPattern(layout,InputGetterLogical);
    const uint8_t *stub=getter?read(getter,8,false):nullptr;
    if(!expected || !stub || Word(stub)!=0x52800020u || Word(expected->code)!=0x52800020u)return 0;
    uintptr_t worker=BranchTarget(expected->address+4,Word(expected->code+4),false);
    if(!worker || worker!=BranchTarget(getter+4,Word(stub+4),false) || !read(worker,4,false))return 0;
    return getter;
}
template<class Reader>
static bool MatchesModern(GenshinRuntimeLayout layout,Reader read,const LayoutSearch &search,
                          uintptr_t &mismatch,Bindings &result) {
    bool global=layout==GenshinRuntimeLayout::GlobalModernInput;
    const auto *patterns=global?GenshinGlobalModernRuntimePatterns:GenshinModernRuntimePatterns;
    size_t count=global?sizeof(GenshinGlobalModernRuntimePatterns)/sizeof(*patterns):
                       sizeof(GenshinModernRuntimePatterns)/sizeof(*patterns);
    uintptr_t layoutAddress=ResolveGenshinAddress(layout,LayoutLogical);
    uintptr_t getterAddress=ResolveGenshinAddress(layout,InputGetterLogical);
    // Discovery only reads bounded code; no candidate is called before the
    // complete ABI and all known callers have been verified below.
    uintptr_t getter=FindInputGetter(layout,read);
    for(size_t i=0;i<count;++i) {
        const auto &p=patterns[i];if(p.address==layoutAddress || p.address==getterAddress)continue;
        const uint8_t *code=read(p.address,sizeof(p.code),false);
        if(!code){mismatch=p.address;return false;}
        for(size_t offset=0;offset<sizeof(p.code);offset+=4) {
            uint32_t before=Word(p.code+offset),after=Word(code+offset);
            bool link=(before&0xfc000000u)==0x94000000u;
            uintptr_t target=BranchTarget(p.address+offset,before,link);
            uintptr_t relocated=target==getterAddress?getter:target==layoutAddress?search.entry:0;
            if(target==getterAddress || target==layoutAddress) {
                // Even unchanged instructions must follow the relocated entry.
                // Otherwise other native readers could still call dead code.
                if(!relocated || BranchTarget(p.address+offset,after,link)!=relocated) {
                    mismatch=p.address+offset;return false;
                }
            } else if(before!=after){mismatch=p.address+offset;return false;}
        }
    }
    uintptr_t statics=global?0x115d94488:0x115ba0c08;
    if(!read(statics,sizeof(void *),true)){mismatch=statics;return false;}
    const auto *expected=GenshinExpectedPattern(layout,LayoutLogical);
    if(search.matches!=1 || !expected || !LayoutMatches(*expected,search.entry,read)) {
        mismatch=layoutAddress;return false;
    }
    if(!getter){mismatch=getterAddress;return false;}
    result={layout,search.entry,getter,search.matches,true};mismatch=0;return true;
}
template<class Reader>
static Bindings Select(Reader read,const CodeSpan *spans,size_t count,uintptr_t &mismatch) {
    Bindings result;auto search=FindLayout(spans,count);result.layoutMatches=search.matches;
    if(MatchesModern(GenshinRuntimeLayout::ModernInput,read,search,mismatch,result) ||
       MatchesModern(GenshinRuntimeLayout::GlobalModernInput,read,search,mismatch,result))return result;
    // Older ABIs retain their complete exact fingerprints and address tables.
    if(MatchesGenshinRuntimeLayout(GenshinRuntimePatterns,
            sizeof(GenshinRuntimePatterns)/sizeof(*GenshinRuntimePatterns),read,mismatch))
        result.layout=GenshinRuntimeLayout::MainlandInput;
    else if(MatchesGenshinRuntimeLayout(GenshinGlobalRuntimePatterns,
            sizeof(GenshinGlobalRuntimePatterns)/sizeof(*GenshinGlobalRuntimePatterns),read,mismatch,0x11551c120))
        result.layout=GenshinRuntimeLayout::GlobalInput;
    return result;
}
static uintptr_t Resolve(const Bindings &b,uintptr_t logical) {
    if(b.layout==GenshinRuntimeLayout::Unsupported)return 0;
    if(b.dynamic) {
        if(logical==LayoutLogical)return b.layoutEntry;
        if(logical==InputGetterLogical)return b.inputGetter;
    }
    return ResolveGenshinAddress(b.layout,logical);
}
}
