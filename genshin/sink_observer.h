#include <atomic>
#include <cmath>
#include <mach/mach_vm.h>
namespace SinkObserver {
struct Ramp {float previous,next;};
struct Buffer {const float *samples;uint32_t channelConfig,state;uint16_t maximum,valid;};
using Init=int (*)(void *,void *,void *,void *,void *);
using Consume=void (*)(void *,const Buffer *,Ramp);
using PCM=void (*)(const float *,unsigned,unsigned,unsigned,unsigned,float,float);
static std::atomic<PCM> privatePCM{nullptr};
static Init originalInit;
static Consume originalConsume;
using Needed=int (*)(void *,uint32_t *);
static Needed originalNeeded;
static void (*originalFrameEnd)(void *);
static std::atomic<uint64_t> neededCalls{0},frameEnds{0};
static int NeededFrames(void *self,uint32_t *count){++neededCalls;return originalNeeded(self,count);}
static void EndFrame(void *self){++frameEnds;originalFrameEnd(self);}
static std::atomic<uint64_t> initializations{0},consumes{0},frames{0},nonzero{0},invalid{0};
static std::atomic<uint32_t> rate{0},channelConfig{0},peakPpm{0};
static std::atomic<void *> captureSink{nullptr};
static std::atomic<bool> selectPrivate{false};
static std::atomic<unsigned> privateRate{0};
static std::atomic<unsigned> captured{0};
static float capture[96000*2];
static std::atomic<uint32_t> gainFirstBits{0},gainLastBits{0};
static int Initialize(void *self,void *allocator,void *context,void *parameters,void *format){
    int result=originalInit(self,allocator,context,parameters,format);
    if(result==1&&format){uint32_t r=0,c=0;memcpy(&r,format,4);memcpy(&c,(char *)format+4,4);rate=r;channelConfig=c;
        if(r==48000&&(c&255)==2&&selectPrivate.load()){privateRate=r;captureSink=self;}
    }
    ++initializations;return result;
}
static void Read(void *self,const Buffer *input,Ramp gain){
    // Read only the middleware-provided native float buffer, preserving the
    // input, gain, return behavior and output routing of the original sink.
    originalConsume(self,input,gain);++consumes;
    if(!input)return;
    unsigned channels=input->channelConfig&255,valid=input->valid,maximum=input->maximum;
    if(!input->samples||channels<1||channels>8||!maximum||maximum>4096||valid>maximum){++invalid;return;}
    uint64_t nz=0;float peak=0;
    for(unsigned ch=0;ch<channels;++ch)for(unsigned i=0;i<valid;++i){
        float value=input->samples[size_t(ch)*maximum+i];
        if(!std::isfinite(value)){++invalid;return;}
        value=std::abs(value);nz+=value>.000001f;peak=std::max(peak,value);
    }
    frames.fetch_add(valid,std::memory_order_relaxed);nonzero.fetch_add(nz,std::memory_order_relaxed);
    if(self==captureSink.load()&&channels==2&&std::isfinite(gain.previous)&&std::isfinite(gain.next)){
        if(auto observer=privatePCM.load(std::memory_order_acquire))
            observer(input->samples,maximum,valid,channels,privateRate.load(),gain.previous,gain.next);
        #if !GDM_NATIVE_PRODUCT
        unsigned start=captured.fetch_add(valid);uint32_t first,last;
        memcpy(&first,&gain.previous,4);memcpy(&last,&gain.next,4);gainFirstBits=first;gainLastBits=last;
        for(unsigned i=0;i<valid&&start+i<96000;++i){
            float volume=gain.previous+(gain.next-gain.previous)*(float(i)/maximum);
            capture[(start+i)*2]=input->samples[i]*volume;
            capture[(start+i)*2+1]=input->samples[maximum+i]*volume;
        }
        #endif
    }
    uint32_t next=(uint32_t)(std::min(peak,100.f)*1e6f),old=peakPpm.load();
    while(next>old&&!peakPpm.compare_exchange_weak(old,next)){}
}
static bool Install(){
    if(originalInit)return true;
    // Derive the correct client data address from its checked sink factory.
    auto code=F<const uint32_t *>(0x110946408);
    if((code[0]&0x9f00001f)!=0x90000008||(code[1]&0xffc003ff)!=0x91000108||code[2]!=0xf9000008)return false;
    int64_t imm=((code[0]>>5&0x7ffff)<<2)|(code[0]>>29&3);if(imm&(1<<20))imm-=1<<21;
    auto table=(void **)(((uintptr_t)code&~uintptr_t(4095))+imm*4096+(code[1]>>10&4095));
    if(table[7]!=F<void *>(0x1109465bc)||table[9]!=F<void *>(0x11094663c)||
       table[8]!=F<void *>(0x11094662c)||table[10]!=F<void *>(0x110946640)){
        W(@"sink_observer refused_vtable=1");return false;
    }
    // Replace complete, aligned vtable pointers. The consume method is only
    // a single RET, so an inline 16-byte patch would corrupt adjacent methods.
    mach_vm_address_t page=(uintptr_t)table&~((uintptr_t)vm_page_size-1),region=page;
    mach_vm_size_t length=0;vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=0;
    kern_return_t kr=mach_vm_region(mach_task_self(),&region,&length,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
    if(object)mach_port_deallocate(mach_task_self(),object);
    if(kr||page<region||page+vm_page_size>region+length)return false;
    kr=mach_vm_protect(mach_task_self(),page,vm_page_size,false,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY);
    if(kr){W(@"sink_observer write_refused=%d",kr);return false;}
    originalInit=(Init)table[7];originalConsume=(Consume)table[9];
    originalNeeded=(Needed)table[8];originalFrameEnd=(void (*)(void *))table[10];
    __atomic_store_n(&table[7],(void *)Initialize,__ATOMIC_RELEASE);
    __atomic_store_n(&table[8],(void *)NeededFrames,__ATOMIC_RELEASE);
    __atomic_store_n(&table[9],(void *)Read,__ATOMIC_RELEASE);
    __atomic_store_n(&table[10],(void *)EndFrame,__ATOMIC_RELEASE);
    kr=mach_vm_protect(mach_task_self(),page,vm_page_size,false,info.protection);
    W(@"sink_observer installed=1 protection_restored=%d no_output_routing_changed=1",kr==KERN_SUCCESS);
    return kr==KERN_SUCCESS;
}
static void Status(){
    W(@"sink_observer status init=%llu needed=%llu frame_ends=%llu consumes=%llu frames=%llu nonzero=%llu invalid=%llu format_word0=%u channels=%u peak=%.6f",
      initializations.load(),neededCalls.load(),frameEnds.load(),consumes.load(),frames.load(),nonzero.load(),invalid.load(),rate.load(),channelConfig.load()&255,peakPpm.exchange(0)/1e6);
}
static void SaveCapture(){
    #if GDM_NATIVE_PRODUCT
    return;
    #else
    unsigned n=std::min(96000u,captured.load()),bytes=n*2*sizeof(float);
    if(!n)return;
    struct Header {char riff[4];uint32_t size;char wave[4],fmt[4];uint32_t fmtSize;uint16_t format,channels;uint32_t rate,byteRate;uint16_t block,bits;char data[4];uint32_t bytes;};
    static_assert(sizeof(Header)==44);
    Header h{{'R','I','F','F'},36+bytes,{'W','A','V','E'},{'f','m','t',' '},16,3,2,48000,48000*8,8,32,{'d','a','t','a'},bytes};
    NSMutableData *wave=[NSMutableData dataWithBytes:&h length:sizeof(h)];[wave appendBytes:capture length:bytes];
    NSString *path=[NSHomeDirectory()stringByAppendingPathComponent:@"Documents/GDMNativeMotionResearch.wav"];
    BOOL ok=[wave writeToFile:path atomically:YES];uint32_t fb=gainFirstBits.load(),lb=gainLastBits.load();float f,l;memcpy(&f,&fb,4);memcpy(&l,&lb,4);
    W(@"sink_observer captured_frames=%u saved=%d gain_previous=%f gain_next=%f hardware_output=0",n,ok,f,l);
    #endif
}
}
