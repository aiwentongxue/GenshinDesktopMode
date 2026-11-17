#pragma once
#import <CoreAudio/CoreAudio.h>
#include "ps_audio_capture.h"
#include "ps_usb_mode.h"
#include "ps_bt_mode.h"
#include "ps_output_owner.h"

namespace PSAudio {
static dispatch_queue_t deviceQueue;
static AudioDeviceID outputDevice=kAudioObjectUnknown;
static AudioDeviceIOProcID outputProc;
static dispatch_source_t modeTimer;
static dispatch_source_t bluetoothTimer;
static PSAuthoredOutput::Owner outputOwner;
static PSBluetooth::Decimator bluetoothDecimator;
static uint64_t bluetoothEpoch,bluetoothSignalEpoch;
static double previousBluetoothTick;
static std::atomic<uint64_t> bluetoothLateTicks{0};
static std::atomic<bool> deviceRunning{false},gameActive{false};
static std::atomic<uint64_t> usbCallbacks{0};
static double previousUsbTick,previousUsbDuration; // Accessed only while holding the consumer lease.
static std::atomic<uint64_t> usbLateTicks{0};
static uint64_t lastUsbCallback;
static double lastUsbActivity,retryUsbStartAt;
static bool usbStreamHealthy;
static unsigned usbRecoveries;
static AudioDeviceID retryUsbDevice=kAudioObjectUnknown;
static std::atomic<uint64_t> outputFrames{0},nonzeroOutputFrames{0},underruns{0},invalidOutputBuffers{0};
static std::atomic<uint64_t> energyNano{0},energySamples{0};
static std::atomic<uint32_t> peakPpm{0};
static std::atomic<unsigned> dualSenseCount{0};
static bool initialized;
static uint64_t reportCaptured,reportOutput;
static void RefreshDevice(void);
static void CloseDevice(void);

static OSStatus DeviceOutput(AudioObjectID,const AudioTimeStamp *,const AudioBufferList *,
                             const AudioTimeStamp *,AudioBufferList *output,const AudioTimeStamp *,void *) {
    if(!output||output->mNumberBuffers>16)return noErr;
    PSAudioOutputMap map;bool valid=true;
    for(unsigned i=0;i<output->mNumberBuffers;++i) {
        auto &b=output->mBuffers[i];
        if(b.mData)memset(b.mData,0,b.mDataByteSize);
        if(!map.add((float *)b.mData,b.mNumberChannels,b.mDataByteSize))valid=false;
    }
    if(!valid||map.count!=4||!map.frames) {++invalidOutputBuffers;return noErr;}
    usbCallbacks.fetch_add(1,std::memory_order_relaxed);
    PSAuthoredOutput::Lease lease(outputOwner,PSAuthoredOutput::Backend::USB);
    if(!lease||!gameActive.load()||!armed.load(std::memory_order_acquire))return noErr;
    const double now=CACurrentMediaTime();
    const bool late=previousUsbTick&&(now<previousUsbTick||now-previousUsbTick>std::max(.05,previousUsbDuration*3));
    previousUsbTick=now;previousUsbDuration=double(map.frames)/48000.;
    if(late){NativeLive::signal.retireBuffered();++usbLateTicks;return noErr;}
    uint64_t epoch=generation.load(),signalEpoch=NativeLive::signal.generation();
    unsigned nonzero=0,missing=0;double energy=0,peak=0;
    for(unsigned i=0;i<map.frames;++i) {
        PSAudioPair pair;
        if(!NativeLive::Read(pair))++missing;
        if(std::abs(pair.left)>.00001f||std::abs(pair.right)>.00001f)++nonzero;
        energy+=double(pair.left)*pair.left+double(pair.right)*pair.right;
        peak=std::max(peak,double(std::max(std::abs(pair.left),std::abs(pair.right))));
        map.write(i,pair);
    }
    if(generation.load()!=epoch||signalEpoch!=NativeLive::signal.generation()||!gameActive.load()||!armed.load()) {
        for(unsigned i=0;i<output->mNumberBuffers;++i)
            if(output->mBuffers[i].mData)memset(output->mBuffers[i].mData,0,output->mBuffers[i].mDataByteSize);
        return noErr;
    }
    outputFrames.fetch_add(map.frames,std::memory_order_relaxed);
    nonzeroOutputFrames.fetch_add(nonzero,std::memory_order_relaxed);
    underruns.fetch_add(missing,std::memory_order_relaxed);
    energyNano.fetch_add((uint64_t)(energy*1e9),std::memory_order_relaxed);
    energySamples.fetch_add(uint64_t(map.frames)*2,std::memory_order_relaxed);
    uint32_t value=(uint32_t)(peak*1e6),old=peakPpm.load();
    while(value>old&&!peakPpm.compare_exchange_weak(old,value)){}
    return noErr;
}
static bool OutputRequested(void) {
    return gameActive.load()&&dualSenseCount.load()==1&&NativeBridge::controllerInputActive.load()&&
        !NativeBridge::nativeOverlayActive.load()&&NativeVibrationSettings::Enabled();
}
static void UpdateArmed(void) {
    const auto backend=outputOwner.current();
    const bool transportReady=(backend==PSAuthoredOutput::Backend::USB&&usbStreamHealthy&&PSUSBMode::accepted.load())||
        (backend==PSAuthoredOutput::Backend::Bluetooth&&PSBTMode::accepted.load());
    const bool next=OutputRequested()&&deviceRunning.load()&&transportReady&&NativeLive::ready.load();
    if(armed.exchange(next)!=next)++generation;
    NativeLive::SetAllowed(next);
}
static void Silence(void) {armed=false;++generation;NativeLive::SetAllowed(false);}
static void RefreshOutputState(void) {
    if(deviceQueue)dispatch_async(deviceQueue,^{RefreshDevice();});
}
static void ObserveGameState(bool foreground) {
    bool next=foreground&&NativeBridge::controllerInputActive.load()&&!NativeBridge::nativeOverlayActive.load();
    if(gameActive.exchange(next)==next)return;
    if(!next)Silence();
    RefreshOutputState();
}
static bool OwnsControllerFeedback(void) {
    return OutputRequested()&&armed.load()&&NativeLive::capabilities.active();
}
static void CloseDevice(void) {
    outputOwner.select(PSAuthoredOutput::Backend::None);
    Silence();deviceRunning=false;
    if(bluetoothTimer)dispatch_source_set_timer(bluetoothTimer,DISPATCH_TIME_FOREVER,DISPATCH_TIME_FOREVER,0);
    if(outputProc) {
        AudioDeviceStop(outputDevice,outputProc);AudioDeviceDestroyIOProcID(outputDevice,outputProc);
        outputProc=nullptr;
    }
    // AudioDeviceStop has completed; the USB consumer can no longer run.
    previousUsbTick=previousUsbDuration=0;
    outputDevice=kAudioObjectUnknown;usbStreamHealthy=false;
    PSUSBMode::Close();
    PSBTMode::Close();bluetoothDecimator.reset();bluetoothEpoch=0;bluetoothSignalEpoch=0;previousBluetoothTick=0;
}
static void BluetoothTick(void) {
    if(outputOwner.current()!=PSAuthoredOutput::Backend::Bluetooth)return;
    if(!OutputRequested()||!NativeLive::ready.load()) {CloseDevice();return;}
    // The first write establishes the transport before the native graph
    // produces anything. Until then, the system motor path remains available.
    if(!PSBTMode::accepted.load()&&!PSBTMode::Send({})) {CloseDevice();return;}
    UpdateArmed();
    if(!armed.load()) {CloseDevice();return;}
    PSAuthoredOutput::Lease lease(outputOwner,PSAuthoredOutput::Backend::Bluetooth);
    if(!lease)return;
    uint64_t epoch=generation.load(),signalEpoch=NativeLive::signal.generation();double now=CACurrentMediaTime();
    if(bluetoothEpoch!=epoch||bluetoothSignalEpoch!=signalEpoch) {
        bluetoothDecimator.reset();bluetoothEpoch=epoch;bluetoothSignalEpoch=signalEpoch;
    }
    PSBluetooth::Samples pcm{};unsigned out=0,missing=0,nonzero=0;
    // After a suspended/stalled worker, retire backlog instead of sending a
    // burst of old effects. Only this consumer touches the queue read pointer.
    if(previousBluetoothTick&&now-previousBluetoothTick>.05) {
        NativeLive::signal.retireBuffered();
        bluetoothDecimator.reset();++bluetoothLateTicks;
    } else for(unsigned i=0;i<PSBluetooth::framesPerPacket*PSBluetooth::decimation;++i) {
        PSAudioPair pair{};if(!NativeLive::Read(pair))++missing;
        if(std::abs(pair.left)>.00001f||std::abs(pair.right)>.00001f)++nonzero;
        int8_t l=0,r=0;
        if(bluetoothDecimator.push(pair.left,pair.right,l,r)) {pcm[out++]=l;pcm[out++]=r;}
    }
    previousBluetoothTick=now;
    if(epoch!=generation.load()||!OutputRequested()||!armed.load()) {CloseDevice();return;}
    if(signalEpoch!=NativeLive::signal.generation()) {
        pcm.fill(0);bluetoothDecimator.reset();bluetoothSignalEpoch=0;
    }
    if(!PSBTMode::Send(pcm)) {CloseDevice();return;}
    outputFrames.fetch_add(PSBluetooth::framesPerPacket*PSBluetooth::decimation,std::memory_order_relaxed);
    nonzeroOutputFrames.fetch_add(nonzero,std::memory_order_relaxed);
    underruns.fetch_add(missing,std::memory_order_relaxed);
}
static bool ReadProperty(AudioObjectID object,AudioObjectPropertySelector selector,AudioObjectPropertyScope scope,
                          void *value,UInt32 &size) {
    AudioObjectPropertyAddress property={selector,scope,kAudioObjectPropertyElementMain};
    return AudioObjectGetPropertyData(object,&property,0,nullptr,&size,value)==noErr;
}
static NSString *DeviceString(AudioObjectID device,AudioObjectPropertySelector selector) {
    CFStringRef result=nullptr;UInt32 size=sizeof(result);
    if(!ReadProperty(device,selector,kAudioObjectPropertyScopeGlobal,&result,size)||!result)return @"";
    return CFBridgingRelease(result);
}
static bool ValidateDevice(AudioDeviceID device,NSString *__strong &name) {
    name=DeviceString(device,kAudioObjectPropertyName);
    NSString *manufacturer=DeviceString(device,kAudioObjectPropertyManufacturer);
    NSString *uid=DeviceString(device,kAudioDevicePropertyDeviceUID);
    // Never select the system default, an arbitrary four-channel sound card,
    // a PS4 controller, a Bluetooth virtual endpoint or an ambiguous pad.
    bool dualSense=[name rangeOfString:@"DualSense" options:NSCaseInsensitiveSearch].location!=NSNotFound||
        ([name isEqualToString:@"Wireless Controller"]&&[manufacturer rangeOfString:@"Sony" options:NSCaseInsensitiveSearch].location!=NSNotFound);
    UInt32 transport=0,size=sizeof(transport);
    if(!dualSense||![uid hasPrefix:@"AppleUSBAudioEngine:"]||
       !ReadProperty(device,kAudioDevicePropertyTransportType,kAudioObjectPropertyScopeGlobal,&transport,size)||
       transport!=kAudioDeviceTransportTypeUSB)return false;
    AudioObjectPropertyAddress property={kAudioDevicePropertyStreams,kAudioDevicePropertyScopeOutput,kAudioObjectPropertyElementMain};
    size=0;
    if(AudioObjectGetPropertyDataSize(device,&property,0,nullptr,&size)||size!=sizeof(AudioStreamID))return false;
    AudioStreamID stream;size=sizeof(stream);
    if(AudioObjectGetPropertyData(device,&property,0,nullptr,&size,&stream))return false;
    AudioStreamBasicDescription format{};size=sizeof(format);
    if(!ReadProperty(stream,kAudioStreamPropertyVirtualFormat,kAudioObjectPropertyScopeGlobal,&format,size))return false;
    return format.mFormatID==kAudioFormatLinearPCM&&(format.mFormatFlags&kAudioFormatFlagIsFloat)&&
        !(format.mFormatFlags&kAudioFormatFlagIsBigEndian)&&format.mBitsPerChannel==32&&
        format.mChannelsPerFrame==4&&std::abs(format.mSampleRate-48000)<1&&
        format.mBytesPerFrame==((format.mFormatFlags&kAudioFormatFlagIsNonInterleaved)?4:16);
}
static void DescribeOutput(AudioDeviceID device) {
    NSMutableArray *levels=[NSMutableArray array];
    for(unsigned channel=0;channel<=4;++channel) {
        Float32 volume=0;UInt32 mute=0,size=sizeof(volume);
        AudioObjectPropertyAddress address={kAudioDevicePropertyVolumeScalar,kAudioDevicePropertyScopeOutput,channel};
        OSStatus volumeStatus=AudioObjectGetPropertyData(device,&address,0,nullptr,&size,&volume);
        address.mSelector=kAudioDevicePropertyMute;size=sizeof(mute);
        OSStatus muteStatus=AudioObjectGetPropertyData(device,&address,0,nullptr,&size,&mute);
        [levels addObject:[NSString stringWithFormat:@"ch%u(volume=%@,mute=%@)",channel,
            volumeStatus?@"unsupported":@(volume),muteStatus?@"unsupported":@(mute)]];
    }
    UInt32 size=sizeof(AudioStreamID);AudioStreamID stream=0;
    if(ReadProperty(device,kAudioDevicePropertyStreams,kAudioDevicePropertyScopeOutput,&stream,size)) {
        for(auto property:{kAudioStreamPropertyVirtualFormat,kAudioStreamPropertyPhysicalFormat}) {
            AudioStreamBasicDescription f{};size=sizeof(f);
            OSStatus status=ReadProperty(stream,property,kAudioObjectPropertyScopeGlobal,&f,size)?0:-1;
            Log(@"ps_audio usb_format physical=%d status=%d rate=%.0f channels=%u bits=%u flags=0x%x bytes_per_frame=%u",
                property==kAudioStreamPropertyPhysicalFormat,status,f.mSampleRate,(unsigned)f.mChannelsPerFrame,
                (unsigned)f.mBitsPerChannel,(unsigned)f.mFormatFlags,(unsigned)f.mBytesPerFrame);
        }
        UInt32 start=0;size=sizeof(start);bool known=ReadProperty(stream,kAudioStreamPropertyStartingChannel,kAudioObjectPropertyScopeGlobal,&start,size);
        Log(@"ps_audio usb_stream starting_channel=%u known=%d",(unsigned)start,known);
    }
    Log(@"ps_audio usb_levels %@ read_only=1",[levels componentsJoinedByString:@" "]);
}
static void RefreshDevice(void) {
    if(!OutputRequested()||!NativeLive::ready.load()) {
        if(deviceRunning.load())CloseDevice();
        return;
    }
    const double now=CACurrentMediaTime();
    AudioObjectPropertyAddress property={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    UInt32 size=0;
    OSStatus status=AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&property,0,nullptr,&size);
    if(status||size>4096||size%sizeof(AudioDeviceID)) {
        CloseDevice();Log(@"ps_audio device_enumeration status=%d bytes=%u",status,(unsigned)size);return;
    }
    std::array<AudioDeviceID,1024> devices{};
    status=AudioObjectGetPropertyData(kAudioObjectSystemObject,&property,0,nullptr,&size,devices.data());
    if(status) {CloseDevice();return;}
    AudioDeviceID candidate=kAudioObjectUnknown;NSString *candidateName=@"";unsigned matches=0;
    for(unsigned i=0;i<size/sizeof(AudioDeviceID);++i) {
        NSString *name;
        if(ValidateDevice(devices[i],name)) {candidate=devices[i];candidateName=name;++matches;}
    }
    if(matches!=1) {
        if(matches==0&&outputOwner.current()==PSAuthoredOutput::Backend::Bluetooth&&deviceRunning) {UpdateArmed();return;}
        CloseDevice();
        // A missing USB audio endpoint is expected for a Bluetooth pad. Open
        // only a unique Sony gamepad whose descriptor advertises report 0x32.
        if(matches==0&&PSBTMode::Open()) {
            outputOwner.select(PSAuthoredOutput::Backend::Bluetooth);deviceRunning=true;UpdateArmed();
            dispatch_source_set_timer(bluetoothTimer,dispatch_time(DISPATCH_TIME_NOW,PSBluetooth::periodNanos),
                PSBluetooth::periodNanos,500000);
            Log(@"ps_audio bluetooth_started=1 authored_audio=1 rate=3000 signed_bits=8 channels=2 packet=0x32 physical_feel_unverified=1");
        }
        return;
    }
    if(outputDevice==candidate&&deviceRunning) {
        const uint64_t count=usbCallbacks.load(std::memory_order_relaxed);
        if(count!=lastUsbCallback) {
            lastUsbCallback=count;
            lastUsbActivity=now;
            usbStreamHealthy=true;
        }
        if(now-lastUsbActivity<3) {
            PSUSBMode::ResumeAudio();
            UpdateArmed();
            return;
        }
        // CoreAudio can retain the same device ID after its IOProc stalls or
        // stops providing the four-channel format. Recreate only our IOProc.
        if(++usbRecoveries<=3||usbRecoveries%20==0)
            Log(@"ps_audio usb_stream_stalled restart=%u valid_callbacks=%llu invalid_output=%llu",
                usbRecoveries,count,invalidOutputBuffers.load());
        retryUsbDevice=candidate;
        retryUsbStartAt=now+1;
        CloseDevice();
        return;
    }
    if(retryUsbDevice==candidate&&now<retryUsbStartAt)return;
    CloseDevice();outputDevice=candidate;
    lastUsbCallback=usbCallbacks.load(std::memory_order_relaxed);
    lastUsbActivity=now;
    DescribeOutput(candidate);
    status=AudioDeviceCreateIOProcID(candidate,DeviceOutput,nullptr,&outputProc);
    if(!status)PSUSBMode::ResumeAudio();
    if(!status) {outputOwner.select(PSAuthoredOutput::Backend::USB);status=AudioDeviceStart(candidate,outputProc);}
    if(status) {
        CloseDevice();
        retryUsbDevice=candidate;retryUsbStartAt=now+3;
        Log(@"ps_audio usb_start_failed status=%d",status);return;
    }
    retryUsbDevice=kAudioObjectUnknown;retryUsbStartAt=0;
    deviceRunning=true;UpdateArmed();
    Log(@"ps_audio usb_started=1 name=%@ actuator_channels=3,4 speaker_channels=silent source=private_native_file_graph authored_file_tracks=1 physical_feel_unverified=1 mode_api_accepted=%d",candidateName,PSUSBMode::accepted.load());
}
static void RefreshControllers(void) {
    unsigned count=0;
    for(GCController *controller in GCController.controllers) {
        if(controller.isSnapshot||!controller.extendedGamepad)continue;
        NSString *vendor=controller.vendorName?:@"";
        if([controller.physicalInputProfile isKindOfClass:GCDualSenseGamepad.class]||
           [vendor rangeOfString:@"DualSense" options:NSCaseInsensitiveSearch].location!=NSNotFound)++count;
    }
    dualSenseCount=count;
    Log(@"ps_audio controller_count=%u preferred_backend=dualsense_usb_pcm bluetooth_fallback=authored_pcm other_controllers=unchanged",count);
    dispatch_async(deviceQueue,^{RefreshDevice();});
}
static void Install(void) {
    if(initialized)return;initialized=true;
    deviceQueue=dispatch_queue_create("local.genshin.ps.audio",DISPATCH_QUEUE_SERIAL);
    bluetoothTimer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,deviceQueue);
    dispatch_source_set_event_handler(bluetoothTimer,^{BluetoothTick();});
    dispatch_source_set_timer(bluetoothTimer,DISPATCH_TIME_FOREVER,DISPATCH_TIME_FOREVER,0);
    dispatch_resume(bluetoothTimer);
    modeTimer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,deviceQueue);
    dispatch_source_set_event_handler(modeTimer,^{RefreshDevice();});
    dispatch_source_set_timer(modeTimer,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),NSEC_PER_SEC,20*NSEC_PER_MSEC);
    dispatch_resume(modeTimer);
    AudioObjectPropertyAddress property={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    OSStatus listener=AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject,&property,deviceQueue,
        ^(UInt32,const AudioObjectPropertyAddress *){RefreshDevice();});
    RefreshControllers();
    auto nc=NSNotificationCenter.defaultCenter;
    for(NSString *name in @[GCControllerDidConnectNotification,GCControllerDidDisconnectNotification,GCControllerDidBecomeCurrentNotification])
        [nc addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *){RefreshControllers();}];
    [nc addObserverForName:@"NSApplicationWillTerminateNotification" object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *){
        Silence();dispatch_async(deviceQueue,^{CloseDevice();});
    }];
    [NSTimer scheduledTimerWithTimeInterval:5 repeats:YES block:^(NSTimer *) {
        uint64_t captured=capturedFrames.load(),output=outputFrames.load(),nonzero=nonzeroOutputFrames.load();
        if(captured!=reportCaptured||output!=reportOutput) {
            uint64_t squares=energyNano.exchange(0),samples=energySamples.exchange(0);
            double rms=samples?std::sqrt(squares/1e9/samples):0;double peak=peakPpm.exchange(0)/1e6;
            Log(@"ps_audio status capture_frames=%llu audible_blocks=%llu output_frames=%llu haptic_frames=%llu active=%d device=%d drops=%llu underruns=%llu invalid_output=%llu pcm_peak=%.5f pcm_rms=%.5f mode_api_accepted=%d mode_writes=%llu mode_failures=%llu backend=%u bt_api_accepted=%d bt_packets=%llu bt_failures=%llu bt_late=%llu usb_late=%llu native_overflows=%llu",
                captured,audibleBlocks.load(),output,nonzero,armed.load(),deviceRunning.load(),droppedFrames.load(),underruns.load(),invalidOutputBuffers.load(),
                peak,rms,PSUSBMode::accepted.load(),PSUSBMode::writes.load(),PSUSBMode::failures.load(),
                unsigned(outputOwner.current()),PSBTMode::accepted.load(),PSBTMode::writes.load(),PSBTMode::failures.load(),bluetoothLateTicks.load(),usbLateTicks.load(),NativeLive::signal.overflows());
            reportCaptured=captured;reportOutput=output;
        }
    }];
    Log(@"ps_audio installed=1 endpoint_listener_status=%d usb_pcm=1 bluetooth_pcm=experimental system_output_unchanged=1 capture=private_native_motion_sink",listener);
}
}
