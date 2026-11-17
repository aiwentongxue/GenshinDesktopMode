#pragma once
#include "runtime_discovery.h"
#include <mach-o/loader.h>
#include <mach/vm_prot.h>
#include <vector>

namespace GenshinRuntimeDiscovery {
// Read only executable __text sections of the main image, never constant data,
// another loaded plugin, or an entire address-space scan. Used once at startup.
template<class Reader>
static bool CodeSpans(const void *data,size_t available,Reader read,std::vector<CodeSpan> &spans) {
    spans.clear();
    if(!data || available<sizeof(mach_header_64))return false;
    const auto *h=(const mach_header_64 *)data;
    if(h->magic!=MH_MAGIC_64 || h->ncmds>1024 || h->sizeofcmds>1024*1024 ||
       h->sizeofcmds>available-sizeof(*h))return false;
    const uint8_t *cursor=(const uint8_t *)(h+1),*end=cursor+h->sizeofcmds;
    for(unsigned i=0;i<h->ncmds;++i) {
        if((size_t)(end-cursor)<sizeof(load_command))return false;
        const auto *c=(const load_command *)cursor;
        if(c->cmdsize<sizeof(*c) || c->cmdsize>(size_t)(end-cursor) || c->cmdsize%8)return false;
        if(c->cmd==LC_SEGMENT_64) {
            if(c->cmdsize<sizeof(segment_command_64))return false;
            const auto *s=(const segment_command_64 *)c;
            if(s->nsects>(c->cmdsize-sizeof(*s))/sizeof(section_64))return false;
            if((s->initprot&(VM_PROT_READ|VM_PROT_EXECUTE))==(VM_PROT_READ|VM_PROT_EXECUTE)) {
                const auto *section=(const section_64 *)(s+1);
                for(unsigned j=0;j<s->nsects;++j) {
                    const auto &t=section[j];
                    if(strncmp(t.sectname,"__text",16) || !(t.flags&(S_ATTR_PURE_INSTRUCTIONS|S_ATTR_SOME_INSTRUCTIONS)))continue;
                    if(!t.size || t.addr<s->vmaddr || t.addr-s->vmaddr>s->vmsize ||
                       t.size>s->vmsize-(t.addr-s->vmaddr) || t.size>SIZE_MAX ||
                       t.size>UINTPTR_MAX-t.addr || spans.size()>=64)return false;
                    const uint8_t *bytes=read(t.addr,(size_t)t.size,false);if(!bytes)return false;
                    spans.push_back({(uintptr_t)t.addr,bytes,(size_t)t.size});
                }
            }
        }
        cursor+=c->cmdsize;
    }
    return cursor==end && !spans.empty();
}
}
