#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <mach/mach.h>
#import <mach/mach_vm.h>
#import <mach-o/dyld.h>
#include <mutex>
#include <cassert>
static void Log(NSString *,...) {}
namespace NativeBridge {static std::atomic<bool> controllerInputActive{false},nativeOverlayActive{false};}
#include "../genshin/ps_audio_haptics.h"
int main() {@autoreleasepool {
    float samples[32];std::fill(std::begin(samples),std::end(samples),1);
    AudioBufferList buffers{};buffers.mNumberBuffers=1;buffers.mBuffers[0]={4,sizeof(samples),samples};
    PSAudio::deviceRunning=true;PSAudio::gameActive=true;PSAudio::UpdateArmed();assert(PSAudio::armed);
    uint64_t generation=PSAudio::generation;
    for(unsigned i=0;i<8;++i)assert(PSAudio::ring.push({.1f,-.2f,generation}));
    assert(!PSAudio::DeviceOutput(0,nullptr,nullptr,nullptr,&buffers,nullptr,nullptr));
    for(unsigned i=0;i<32;++i)assert(samples[i]==(i%4==2?.1f:i%4==3?-.2f:0));
    assert(PSAudio::outputFrames==8&&PSAudio::nonzeroOutputFrames==8);
    assert(PSAudio::energySamples==16&&PSAudio::energyNano>399999900&&PSAudio::energyNano<400000100);
    assert(PSAudio::peakPpm==200000);
    PSAudio::gameActive=false;PSAudio::UpdateArmed();assert(!PSAudio::armed);
    assert(PSAudio::generation!=generation);assert(PSAudio::ring.push({.2f,.2f,generation}));
    PSAudio::DeviceOutput(0,nullptr,nullptr,nullptr,&buffers,nullptr,nullptr);
    for(float f:samples)assert(f==0);
    PSAudio::gameActive=true;PSAudio::UpdateArmed();
    PSAudio::DeviceOutput(0,nullptr,nullptr,nullptr,&buffers,nullptr,nullptr);
    for(float f:samples)assert(f==0); // no replay after focus/mode transition
    std::fill(std::begin(samples),std::end(samples),1);buffers.mBuffers[0].mNumberChannels=2;
    PSAudio::DeviceOutput(0,nullptr,nullptr,nullptr,&buffers,nullptr,nullptr);
    for(float f:samples)assert(f==0);assert(PSAudio::invalidOutputBuffers==1);
    AudioObjectPropertyAddress property={kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    AudioDeviceID before=0,after=0;UInt32 size=sizeof(before);
    OSStatus status=AudioObjectGetPropertyData(kAudioObjectSystemObject,&property,0,nullptr,&size,&before);assert(!status);
    NSString *name;assert(!PSAudio::ValidateDevice(before,name)); // built-in output must never be selected
    size=sizeof(after);assert(!AudioObjectGetPropertyData(kAudioObjectSystemObject,&property,0,nullptr,&size,&after));assert(before==after);
    puts("PS audio backend: actual production output callback maps only actuator channels, mode/focus silences and drops old audio, stereo rejected, default device untouched; no device started");
}}
