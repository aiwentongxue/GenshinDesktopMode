#pragma once
#include "../genshin/runtime_layouts.h"
#include <mach-o/loader.h>
#include <mach/vm_prot.h>
#include <vector>
#include <fstream>
#include <iterator>
#include <cassert>

struct Image {
    std::vector<uint8_t> bytes;
    std::vector<segment_command_64> segments;
    bool dataPresent=true;
    explicit Image(const char *path) {
        std::ifstream input(path,std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(input),{});
        assert(bytes.size()>sizeof(mach_header_64));
        auto *header=(mach_header_64 *)bytes.data();assert(header->magic==MH_MAGIC_64);
        auto *command=(load_command *)(header+1);bool uuidChanged=false;
        for(uint32_t i=0;i<header->ncmds;++i) {
            if(command->cmd==LC_SEGMENT_64) segments.push_back(*(segment_command_64 *)command);
            if(command->cmd==LC_UUID) {
                ((uuid_command *)command)->uuid[0]^=0xff;uuidChanged=true;
            }
            command=(load_command *)((char *)command+command->cmdsize);
        }
        assert(uuidChanged);
    }
    const uint8_t *read(uintptr_t address,size_t length,bool writable) {
        for(auto &s:segments) {
            int required=writable?(VM_PROT_READ|VM_PROT_WRITE):(VM_PROT_READ|VM_PROT_EXECUTE);
            if((s.initprot&required)!=required || address<s.vmaddr || address-s.vmaddr>s.vmsize ||
                    length>s.vmsize-(address-s.vmaddr)) continue;
            if(writable) return dataPresent?bytes.data():nullptr;
            auto off=s.fileoff+address-s.vmaddr;
            if(off>bytes.size() || length>bytes.size()-off) return nullptr;
            return bytes.data()+off;
        }
        return nullptr;
    }
};
