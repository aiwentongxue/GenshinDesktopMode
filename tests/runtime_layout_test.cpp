#include "../genshin/runtime_layout_profile.h"
#include <mach-o/loader.h>
#include <mach/vm_prot.h>
#include <vector>
#include <fstream>
#include <iterator>
#include <cassert>
#include <iostream>

int main(int argc,char **argv) {
    assert(argc==2);
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    assert(bytes.size()>sizeof(mach_header_64));
    auto *header=(mach_header_64 *)bytes.data();assert(header->magic==MH_MAGIC_64);
    auto *command=(load_command *)(header+1);
    std::vector<segment_command_64> segments;
    bool changedUUID=false;
    for(uint32_t i=0;i<header->ncmds;++i) {
        if(command->cmd==LC_SEGMENT_64) segments.push_back(*(segment_command_64 *)command);
        if(command->cmd==LC_UUID) {
            auto *uuid=(uuid_command *)command;uuid->uuid[0]^=0xff;changedUUID=true;
        }
        command=(load_command *)((char *)command+command->cmdsize);
    }
    assert(changedUUID);
    bool dataPresent=true;
    auto reader=[&](uintptr_t address,size_t length,bool writable)->const uint8_t * {
        for(auto &s:segments) {
            int permissions=writable?(VM_PROT_READ|VM_PROT_WRITE):(VM_PROT_READ|VM_PROT_EXECUTE);
            if((s.initprot&permissions)!=permissions || address<s.vmaddr ||
               address-s.vmaddr>s.vmsize || length>s.vmsize-(address-s.vmaddr)) continue;
            if(writable) return dataPresent?bytes.data():nullptr;
            auto off=s.fileoff+address-s.vmaddr;
            if(off>bytes.size() || length>bytes.size()-off) return nullptr;
            return bytes.data()+off;
        }
        return nullptr;
    };
    constexpr size_t count=sizeof(GenshinRuntimePatterns)/sizeof(GenshinRuntimePatterns[0]);
    uintptr_t mismatch=0;
    assert(MatchesGenshinRuntimeLayout(GenshinRuntimePatterns,count,reader,mismatch));
    // A repacking UUID change remains eligible; an actual engine body change
    // is refused before any engine pointer can be called or patched.
    auto &first=GenshinRuntimePatterns[0];
    uint8_t *code=const_cast<uint8_t *>(reader(first.address,96,false));
    code[40]^=1;
    assert(!MatchesGenshinRuntimeLayout(GenshinRuntimePatterns,count,reader,mismatch));
    assert(mismatch==first.address);code[40]^=1;
    dataPresent=false;
    assert(!MatchesGenshinRuntimeLayout(GenshinRuntimePatterns,count,reader,mismatch));
    assert(mismatch==0x115644a70);
    assert(!MatchesGenshinRuntimeLayout(nullptr,0,reader,mismatch));
    std::cout<<"Runtime layout: changed UUID accepted; code corruption and missing writable data rejected\n";
}
