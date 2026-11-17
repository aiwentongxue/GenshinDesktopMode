#import <Foundation/Foundation.h>
#import <mach/mach.h>
#import <mach/mach_vm.h>
#import <mach-o/dyld.h>
#include <mutex>
#include <cassert>
static void Log(NSString *,...) {}
#include "../genshin/ps_audio_capture.h"
static OSStatus callbackResult;
static unsigned callbacks;
static OSStatus Original(void *ref,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *time,UInt32 bus,UInt32 frames,AudioBufferList *data) {
    assert(ref==(void *)123&&(*flags==1||*flags==(1|kAudioUnitRenderAction_OutputIsSilence))&&time&&bus==0&&frames==16&&data);++callbacks;return callbackResult;
}
int main() {@autoreleasepool {
    AudioStreamBasicDescription f{};f.mSampleRate=48000;f.mFormatID=kAudioFormatLinearPCM;
    f.mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked;f.mBytesPerFrame=8;f.mBitsPerChannel=32;f.mChannelsPerFrame=2;
    uint64_t format=PSAudio::PackFormat(f);assert(format);
    f.mFormatFlags|=kAudioFormatFlagIsBigEndian;assert(!PSAudio::PackFormat(f));f.mFormatFlags^=kAudioFormatFlagIsBigEndian;
    float samples[32];for(unsigned i=0;i<32;++i)samples[i]=std::sin(i*.3f)*.3f;
    float original[32];memcpy(original,samples,sizeof(samples));
    AudioBufferList buffers{};buffers.mNumberBuffers=1;buffers.mBuffers[0]={2,sizeof(samples),samples};
    PSAudio::CaptureContext context;context.format=format;context.callback={(AURenderCallback)Original,(void *)123};
    PSAudio::armed=true;AudioUnitRenderActionFlags flags=1;AudioTimeStamp time{};
    assert(!PSAudio::Render(&context,&flags,&time,0,16,&buffers));assert(callbacks==1&&flags==1);
    assert(!memcmp(samples,original,sizeof(samples)));assert(PSAudio::capturedFrames==16&&PSAudio::ring.written==16);
    callbackResult=-123;assert(PSAudio::Render(&context,&flags,&time,0,16,&buffers)==-123);assert(PSAudio::capturedFrames==16);
    callbackResult=0;flags|=kAudioUnitRenderAction_OutputIsSilence;
    assert(!PSAudio::Render(&context,&flags,&time,0,16,&buffers));assert(PSAudio::capturedFrames==16);flags=1;
    buffers.mBuffers[0].mDataByteSize=4;PSAudio::Capture(context,16,&buffers);assert(PSAudio::capturedFrames==16);
    buffers.mBuffers[0].mDataByteSize=sizeof(samples);
    f.mFormatFlags|=kAudioFormatFlagIsNonInterleaved;f.mBytesPerFrame=4;context.format=PSAudio::PackFormat(f);
    PSAudio::Capture(context,16,&buffers);assert(PSAudio::capturedFrames==16); // no out-of-bounds second buffer
    context.format=format;PSAudio::armed=false;PSAudio::Capture(context,16,&buffers);
    assert(PSAudio::capturedFrames==32&&PSAudio::ring.written==16); // metrics only without a controller
    context.disposed=true;PSAudio::Capture(context,16,&buffers);assert(PSAudio::capturedFrames==32);
    PSAudio::CaptureContext replacement;replacement.format=format;
    PSAudio::Capture(replacement,16,&buffers);assert(PSAudio::capturedFrames==48);
    assert(PSAudio::source.load()==(uintptr_t)&replacement);
    puts("PS audio capture: original callback/result/flags/data preserved, buffer bounds, disabled output and disposed-source rejection passed; imports not patched, no hardware output");
}}
