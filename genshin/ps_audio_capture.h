#pragma once
#import <AudioToolbox/AudioToolbox.h>
#include <mach-o/nlist.h>
#include "ps_audio_signal.h"

namespace PSAudio {
static PSAudioRing ring;
static std::atomic<bool> armed{false};
static std::atomic<uint64_t> generation{1},capturedFrames{0},audibleBlocks{0},droppedFrames{0};
static std::atomic<uintptr_t> source{0};
static std::atomic_flag producerBusy=ATOMIC_FLAG_INIT;
struct CaptureContext {
    AudioUnit unit=nullptr;AURenderCallbackStruct callback{};
    std::atomic<uint64_t> format{0};PSAudioTransform transform;
    std::atomic<bool> disposed{false};
};
static CaptureContext contexts[64];
static unsigned contextCount;
static std::mutex contextMutex;
static decltype(&AudioUnitSetProperty) originalSetProperty=&AudioUnitSetProperty;
static decltype(&AudioOutputUnitStart) originalStart=&AudioOutputUnitStart;
static decltype(&AudioOutputUnitStop) originalStop=&AudioOutputUnitStop;
static decltype(&AudioComponentInstanceDispose) originalDispose=&AudioComponentInstanceDispose;

static uint64_t PackFormat(const AudioStreamBasicDescription &f) {
    if(f.mFormatID!=kAudioFormatLinearPCM||!std::isfinite(f.mSampleRate)||f.mSampleRate<8000||f.mSampleRate>192000||
       !f.mChannelsPerFrame||f.mChannelsPerFrame>8||(f.mFormatFlags&kAudioFormatFlagIsBigEndian)||
       !(f.mFormatFlags&kAudioFormatFlagIsPacked))return 0;
    unsigned kind=(f.mFormatFlags&kAudioFormatFlagIsFloat)?(f.mBitsPerChannel==32?1:0):
        (f.mFormatFlags&kAudioFormatFlagIsSignedInteger)?(f.mBitsPerChannel==16?2:f.mBitsPerChannel==32?3:0):0;
    unsigned separate=(f.mFormatFlags&kAudioFormatFlagIsNonInterleaved)!=0;
    unsigned width=kind==2?2:4;
    if(!kind||f.mBytesPerFrame!=width*(separate?1:f.mChannelsPerFrame))return 0;
    return uint64_t(std::llround(f.mSampleRate))|(uint64_t(kind)<<32)|
        (uint64_t(f.mChannelsPerFrame)<<36)|(uint64_t(separate)<<40);
}
static bool GameOutputUnit(AudioUnit unit) {
    AudioComponentDescription d{};
    auto component=AudioComponentInstanceGetComponent(unit);
    return component&&!AudioComponentGetDescription(component,&d)&&d.componentType==kAudioUnitType_Output&&
        // macOS output subtype identifiers from AUComponent.h; the Catalyst
        // headers expose only the RemoteIO spelling, but host units can use
        // the same public 'def ' and 'ahal' component identifiers.
        (d.componentSubType==0x72696f63u||d.componentSubType==0x64656620u||
         d.componentSubType==0x6168616cu);
}
static void RefreshFormat(CaptureContext &context) {
    AudioStreamBasicDescription f{};UInt32 size=sizeof(f);
    OSStatus status=AudioUnitGetProperty(context.unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&f,&size);
    uint64_t packed=status?0:PackFormat(f);context.format.store(packed,std::memory_order_release);
    Log(@"ps_audio capture_format status=%d valid=%d rate=%.0f channels=%u bits=%u flags=0x%x",status,packed!=0,
        f.mSampleRate,(unsigned)f.mChannelsPerFrame,(unsigned)f.mBitsPerChannel,(unsigned)f.mFormatFlags);
}
static float ReadSample(const void *data,size_t index,unsigned kind) {
    if(kind==1) {float value;memcpy(&value,(const char *)data+index*4,4);return value;}
    if(kind==2) {int16_t value;memcpy(&value,(const char *)data+index*2,2);return value/32768.f;}
    int32_t value;memcpy(&value,(const char *)data+index*4,4);return (float)(value/2147483648.);
}
static void Capture(CaptureContext &context,UInt32 frames,const AudioBufferList *data) {
    uint64_t format=context.format.load(std::memory_order_acquire);
    if(context.disposed.load()||!format||!data||!frames||frames>16384)return;
    unsigned kind=(format>>32)&15,channels=(format>>36)&15,separate=(format>>40)&1,width=kind==2?2:4;
    unsigned right=channels>1?1:0;
    if(!data->mNumberBuffers||data->mNumberBuffers>8||data->mNumberBuffers<(separate?channels:1))return;
    const AudioBuffer &leftBuffer=data->mBuffers[0];
    const AudioBuffer &rightBuffer=data->mBuffers[separate?right:0];
    unsigned stride=separate?1:channels;
    if(!leftBuffer.mData||!rightBuffer.mData||
       leftBuffer.mNumberChannels!=(separate?1:channels)||rightBuffer.mNumberChannels!=(separate?1:channels)||
       leftBuffer.mDataByteSize<size_t(frames)*stride*width||rightBuffer.mDataByteSize<size_t(frames)*stride*width)return;
    uintptr_t wanted=source.load(),identity=(uintptr_t)&context;
    if(!wanted||((CaptureContext *)wanted)->disposed.load())source.compare_exchange_strong(wanted,identity);
    if(source.load()!=identity)return;
    capturedFrames.fetch_add(frames,std::memory_order_relaxed);
    // Aggregate evidence only: never record or export audio samples.
    for(unsigned i=0;i<std::min(frames,16u);++i)
        if(std::abs(ReadSample(leftBuffer.mData,i*stride,kind))>.0001f) {++audibleBlocks;break;}
    if(!armed.load(std::memory_order_acquire)||producerBusy.test_and_set(std::memory_order_acquire))return;
    uint64_t epoch=generation.load();double rate=format&0xffffffffu;
    if(context.transform.rate!=rate||context.transform.generation!=epoch)context.transform.reset(rate,epoch);
    for(unsigned i=0;i<frames;++i) {
        float l=ReadSample(leftBuffer.mData,i*stride,kind);
        float r=ReadSample(rightBuffer.mData,i*stride+(separate?0:right),kind);
        context.transform.sample(l,r,[](PSAudioPair sample){if(!ring.push(sample))++droppedFrames;});
    }
    producerBusy.clear(std::memory_order_release);
}
static OSStatus Render(void *ref,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *time,
                       UInt32 bus,UInt32 frames,AudioBufferList *data) {
    auto &context=*(CaptureContext *)ref;
    OSStatus status=context.callback.inputProc(context.callback.inputProcRefCon,flags,time,bus,frames,data);
    if(!status&&bus==0&&(!flags||!(*flags&kAudioUnitRenderAction_OutputIsSilence)))Capture(context,frames,data);
    return status;
}
static OSStatus SetProperty(AudioUnit unit,AudioUnitPropertyID property,AudioUnitScope scope,
                            AudioUnitElement element,const void *value,UInt32 size) {
    if(property==kAudioUnitProperty_SetRenderCallback&&scope==kAudioUnitScope_Input&&element==0&&value&&
       size==sizeof(AURenderCallbackStruct)&&GameOutputUnit(unit)) {
        auto callback=*(const AURenderCallbackStruct *)value;
        if(callback.inputProc&&callback.inputProc!=(AURenderCallback)Render) {
            std::lock_guard<std::mutex> lock(contextMutex);
            if(contextCount<64) {
                CaptureContext &context=contexts[contextCount++];context.unit=unit;context.callback=callback;
                RefreshFormat(context);
                AURenderCallbackStruct wrapped={Render,&context};
                OSStatus result=originalSetProperty(unit,property,scope,element,&wrapped,sizeof(wrapped));
                if(result)context.disposed=true;
                else {
                    // A successful replacement retires the previous source.
                    // Context storage stays alive for any callback in flight.
                    for(unsigned i=0;i+1<contextCount;++i)if(contexts[i].unit==unit)contexts[i].disposed=true;
                    ++generation;
                }
                Log(@"ps_audio capture_callback status=%d original_preserved=1 own_process_only=1",result);
                return result;
            }
            Log(@"ps_audio capture_callback capacity_reached=1 original_unchanged=1");
        }
    }
    OSStatus result=originalSetProperty(unit,property,scope,element,value,size);
    if(!result&&property==kAudioUnitProperty_SetRenderCallback&&scope==kAudioUnitScope_Input&&element==0&&
       (!value||size!=sizeof(AURenderCallbackStruct)||((const AURenderCallbackStruct *)value)->inputProc!=(AURenderCallback)Render)) {
        std::lock_guard<std::mutex> lock(contextMutex);
        for(unsigned i=0;i<contextCount;++i)if(contexts[i].unit==unit)contexts[i].disposed=true;
        ++generation;
    }
    if(!result&&property==kAudioUnitProperty_StreamFormat&&scope==kAudioUnitScope_Input&&element==0) {
        std::lock_guard<std::mutex> lock(contextMutex);
        for(unsigned i=0;i<contextCount;++i)if(contexts[i].unit==unit&&!contexts[i].disposed)RefreshFormat(contexts[i]);
    }
    return result;
}
static OSStatus Start(AudioUnit unit) {
    {std::lock_guard<std::mutex> lock(contextMutex);
     for(unsigned i=0;i<contextCount;++i)if(contexts[i].unit==unit&&!contexts[i].disposed)RefreshFormat(contexts[i]);}
    return originalStart(unit);
}
static void Retire(AudioUnit unit,bool dispose) {
    std::lock_guard<std::mutex> lock(contextMutex);
    for(unsigned i=0;i<contextCount;++i)if(contexts[i].unit==unit) {
        if(dispose)contexts[i].disposed=true;
        uintptr_t identity=(uintptr_t)&contexts[i];source.compare_exchange_strong(identity,0);
    }
    ++generation;
}
static OSStatus Stop(AudioUnit unit) {OSStatus result=originalStop(unit);if(!result)Retire(unit,false);return result;}
static OSStatus Dispose(AudioComponentInstance unit) {OSStatus result=originalDispose(unit);if(!result)Retire(unit,true);return result;}

static bool StoreImport(uintptr_t *slot,uintptr_t value) {
    mach_vm_address_t region=(mach_vm_address_t)slot;mach_vm_size_t length=0;
    vm_region_basic_info_data_64_t info{};mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=0;
    if(mach_vm_region(mach_task_self(),&region,&length,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object)!=KERN_SUCCESS)return false;
    if(object)mach_port_deallocate(mach_task_self(),object);
    if((uintptr_t)slot<region||(uintptr_t)slot+sizeof(*slot)>region+length||(info.protection&VM_PROT_EXECUTE))return false;
    mach_vm_address_t page=(uintptr_t)slot&~(uintptr_t(vm_page_size)-1);
    if(mach_vm_protect(mach_task_self(),page,vm_page_size,false,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS)return false;
    __atomic_store_n(slot,value,__ATOMIC_RELEASE);
    bool restored=mach_vm_protect(mach_task_self(),page,vm_page_size,false,info.protection)==KERN_SUCCESS;
    return restored;
}
static void InstallAudioImports(void) {
    static bool installed;if(installed)return;installed=true;
    const auto *header=(const mach_header_64 *)_dyld_get_image_header(0);
    intptr_t slide=_dyld_get_image_vmaddr_slide(0);
    if(!header||header->magic!=MH_MAGIC_64||header->ncmds>1024)return;
    const symtab_command *symbols=nullptr;const dysymtab_command *dynamic=nullptr;const segment_command_64 *link=nullptr;
    auto command=(const load_command *)(header+1);const char *end=(const char *)(header+1)+header->sizeofcmds;
    for(unsigned i=0;i<header->ncmds;++i) {
        if((const char *)(command+1)>end||command->cmdsize<sizeof(*command)||(const char *)command+command->cmdsize>end)return;
        if(command->cmd==LC_SYMTAB)symbols=(const symtab_command *)command;
        if(command->cmd==LC_DYSYMTAB)dynamic=(const dysymtab_command *)command;
        if(command->cmd==LC_SEGMENT_64&&!strcmp(((const segment_command_64 *)command)->segname,"__LINKEDIT"))link=(const segment_command_64 *)command;
        command=(const load_command *)((const char *)command+command->cmdsize);
    }
    if(!symbols||!dynamic||!link)return;
    uintptr_t base=slide+link->vmaddr-link->fileoff;
    auto names=(const char *)(base+symbols->stroff);
    auto table=(const nlist_64 *)(base+symbols->symoff);
    auto indirect=(const uint32_t *)(base+dynamic->indirectsymoff);
    const char *targets[]={"_AudioUnitSetProperty","_AudioOutputUnitStart","_AudioOutputUnitStop","_AudioComponentInstanceDispose"};
    void *replacements[]={(void *)SetProperty,(void *)Start,(void *)Stop,(void *)Dispose};
    unsigned patched[4]{};
    command=(const load_command *)(header+1);
    for(unsigned i=0;i<header->ncmds;++i,command=(const load_command *)((const char *)command+command->cmdsize)) {
        if(command->cmd!=LC_SEGMENT_64)continue;
        auto segment=(const segment_command_64 *)command;auto sections=(const section_64 *)(segment+1);
        if(sizeof(*segment)+segment->nsects*sizeof(*sections)>command->cmdsize)return;
        for(unsigned j=0;j<segment->nsects;++j) {
            auto &section=sections[j];unsigned type=section.flags&SECTION_TYPE;
            if(type!=S_LAZY_SYMBOL_POINTERS&&type!=S_NON_LAZY_SYMBOL_POINTERS)continue;
            uint64_t length=section.size/sizeof(uintptr_t);
            if(section.reserved1+length>dynamic->nindirectsyms)continue;
            auto slots=(uintptr_t *)(slide+section.addr);
            for(uint64_t k=0;k<length;++k) {
                uint32_t symbol=indirect[section.reserved1+k];
                if(symbol>=symbols->nsyms)continue;
                uint32_t offset=table[symbol].n_un.n_strx;
                if(offset>=symbols->strsize||!memchr(names+offset,0,symbols->strsize-offset))continue;
                for(unsigned n=0;n<4;++n)if(!strcmp(names+offset,targets[n]))
                    if(StoreImport(slots+k,(uintptr_t)replacements[n]))++patched[n];
            }
        }
    }
    Log(@"ps_audio imports property=%u start=%u stop=%u dispose=%u scope=main_executable no_audio_samples_recorded=1",patched[0],patched[1],patched[2],patched[3]);
}
}
