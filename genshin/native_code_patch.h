#pragma once
#include <sys/mman.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
namespace NativeCodePatch {
inline void Jump(void *dst,uintptr_t address){
    uint32_t ops[]={0x58000050,0xd61f0200};memcpy(dst,ops,8);memcpy((char *)dst+8,&address,8);
}
template<class T>inline bool Install(void *target,void *replacement,const uint32_t (&expected)[4],std::atomic<T> &original){
    if(memcmp(target,expected,16))return false;
    void *trampoline=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(trampoline==MAP_FAILED)return false;
    memcpy(trampoline,target,16);Jump((char *)trampoline+16,(uintptr_t)target+16);
    sys_icache_invalidate(trampoline,32);
    if(mprotect(trampoline,4096,PROT_READ|PROT_EXEC)){munmap(trampoline,4096);return false;}
    auto page=(uintptr_t)target&~(uintptr_t(vm_page_size)-1);
    if(mach_vm_protect(mach_task_self(),page,vm_page_size,false,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)){
        munmap(trampoline,4096);return false;
    }
    original.store((T)trampoline,std::memory_order_release);
    uint8_t jump[16];Jump(jump,(uintptr_t)replacement);memcpy(target,jump,16);sys_icache_invalidate(target,16);
    // The active relay owns this trampoline for the process lifetime, including
    // the case where restoring page protection fails after the patch.
    kern_return_t result=mach_vm_protect(mach_task_self(),page,vm_page_size,false,VM_PROT_READ|VM_PROT_EXECUTE);
    W(@"native_live code_patch execute_protection=%d",result);
    return true;
}
}
